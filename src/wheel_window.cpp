// Task 6: radial wheel overlay window.
// Click-through layered window; Direct2D (ID2D1DCRenderTarget) renders into a
// 32bpp top-down DIB section, composited with UpdateLayeredWindow(AC_SRC_ALPHA).
// See wheel_window.h for interface contract, threading/COM and DPI notes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "wheel_window.h"
#include "icon_lookup.h"
#include "icon_cache.h"

#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using WheelWindow::Frame;

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT     kTimerMs = 8;        // ~120fps repaint while active
constexpr double   kPopDurationMs = 120;
constexpr double   kShadowMargin = 12.0;
constexpr double   kArcGap = 0.012;     // radian gap between sectors
constexpr int      kMaxLabelSectors = 12;
constexpr double   kMaxArcStep = M_PI / 2.0; // 90 degrees per AddArc segment

float EaseOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// UTF-8 (as stored in config) -> UTF-16 for DirectWrite.
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Append a (possibly split) arc to an open geometry sink. The current point
// must already be at startAngle on the circle; the walk ends at endAngle.
// A single ID2D1ArcSegment cannot express >180 degrees and with n=3 sectors a
// sector spans 120 degrees, so every arc is walked in <=90-degree sub-arcs.
// clockwise=true: startAngle < endAngle (increasing); false: decreasing.
void AddArcSplit(ID2D1GeometrySink* sink, const D2D1_POINT_2F center, double r,
                 double startAngle, double endAngle, bool clockwise) {
    auto anglePoint = [&](double angA) {
        return D2D1::Point2F(static_cast<FLOAT>(center.x + r * std::cos(angA)),
                             static_cast<FLOAT>(center.y + r * std::sin(angA)));
    };
    const double span = std::max(0.0, clockwise ? endAngle - startAngle
                                                : startAngle - endAngle);
    const int steps = std::max(1, (int)std::ceil(span / kMaxArcStep - 1e-9));
    const double step = span / steps;
    for (int i = 0; i < steps; ++i) {
        const double cur = clockwise ? startAngle + step * i
                                     : startAngle - step * i;
        const double nxt = clockwise ? startAngle + step * (i + 1)
                                     : startAngle - step * (i + 1);
        const D2D1_ARC_SEGMENT seg = D2D1::ArcSegment(
            anglePoint(nxt),
            D2D1::SizeF((FLOAT)r, (FLOAT)r),
            0.0f,
            clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                      : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL);
        sink->AddArc(seg);
    }
}

// Classic annular sector: outer arc CW a0->a1, line to inner arc at a1,
// inner arc CCW a1->a0, close. Caller releases.
ID2D1PathGeometry* BuildSectorGeometry(ID2D1Factory* factory,
                                       const D2D1_POINT_2F center,
                                       double inner, double outer,
                                       double a0, double a1) {
    ID2D1PathGeometry* geo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geo)) || !geo) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geo->Open(&sink)) || !sink) {
        geo->Release();
        return nullptr;
    }
    auto pt = [&](double r, double angA) {
        return D2D1::Point2F(static_cast<FLOAT>(center.x + r * std::cos(angA)),
                             static_cast<FLOAT>(center.y + r * std::sin(angA)));
    };
    sink->BeginFigure(pt(outer, a0), D2D1_FIGURE_BEGIN_FILLED);
    AddArcSplit(sink, center, outer, a0, a1, /*clockwise=*/true);
    sink->AddLine(pt(inner, a1));
    AddArcSplit(sink, center, inner, a1, a0, /*clockwise=*/false);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return geo;
}

struct ThemeColors {
    D2D1_COLOR_F fill, highlight, border, text, textSel;
};

ThemeColors ThemeFor(const std::string& theme) {
    if (theme == "light")
        return { D2D1::ColorF(0.96f, 0.96f, 0.98f, 0.92f),
                 D2D1::ColorF(0.35f, 0.22f, 0.85f, 0.95f),
                 D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f),
                 D2D1::ColorF(0.1f, 0.1f, 0.12f, 0.95f),
                 D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.98f) };
    // "dark" and any unknown string
    return { D2D1::ColorF(0.086f, 0.086f, 0.10f, 0.85f),
             D2D1::ColorF(0.42f, 0.30f, 1.00f, 0.92f),
             D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f),
             D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.92f),
             D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f) };
}

// M3d：文字/图标描边光晕 —— 轮盘是半透明悬浮层，白字叠在浅色桌面上对比
// 不足（用户反馈"实际使用看不清"）。先以四向偏移画 glow 色文字再画正文。
void DrawTextGlow(ID2D1RenderTarget* rt, const wchar_t* text, UINT32 len,
                  IDWriteTextFormat* fmt, const D2D1_RECT_F& box,
                  ID2D1SolidColorBrush* fill, ID2D1SolidColorBrush* glow, FLOAT r) {
    if (glow && r > 0.0f) {
        const FLOAT o[4][2] = {{-r,-r},{r,-r},{-r,r},{r,r}};
        for (auto& d : o) {
            const D2D1_RECT_F b{ box.left + d[0], box.top + d[1],
                                 box.right + d[0], box.bottom + d[1] };
            rt->DrawText(text, len, fmt, b, glow);
        }
    }
    rt->DrawText(text, len, fmt, box, fill);
}

// opacity 档位 -> alpha 乘数（spec §6）。mid=0.85 与 M1 现状逐位一致。
float OpacityMul(const std::string& opacity) {
    if (opacity == "low") return 0.55f;
    if (opacity == "high") return 1.0f;
    return 0.85f;  // mid 与未知值
}

struct WheelState {
    HINSTANCE hinst = nullptr;
    HWND hwnd = nullptr;
    bool active = false;

    // D2D / DWrite resources (created in Create, alive for process lifetime)
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1DCRenderTarget* rt = nullptr;
    IDWriteFactory* dwFactory = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    IDWriteTextFormat* glyphFormat = nullptr;  // M3c: Segoe MDL2 Assets（主环，尺寸=iconSize）
    IDWriteTextFormat* glyphFormatSub = nullptr;  // M3c：子环字形（受子环宽度封顶）
    float glyphPx = 0, glyphSubPx = 0;

    // DIB section used as the layered-window bitmap
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
    void* dibBits = nullptr;
    int side = 0; // current DIB width/height in px

    // Current frame content
    Profile profile;
    AppearanceSettings app;
    double dpiScale = 1.0;     // M1.1-②：宿主注入的 dpi/96（字号/线宽/标签框）
    bool hasAnySub = false;    // M3b：profile 任一扇区有子动作 -> 窗口预留子环空间
    ULONGLONG subT0 = 0;       // showSub false->true 时刻（展开动画起表）
    int lastSel = -1;          // M3c disc：选中变化时刻（按钮 1.15x 过渡）
    ULONGLONG selT0 = 0;
    POINT origin{};        // top-left of window on screen
    POINT centerLocal{};   // wheel center inside the DIB
    Frame frame;
    ULONGLONG t0 = 0;      // pop-animation start (GetTickCount64 ms)

    // Theme brushes (per Show)
    ID2D1SolidColorBrush *fill = nullptr, *highlight = nullptr, *border = nullptr,
                          *text = nullptr, *textSel = nullptr, *pointer = nullptr;
    ID2D1SolidColorBrush* textGlow = nullptr;  // M3d：文字描边光晕（主题自适应）

    void ReleaseDib() {
        if (dib) { DeleteObject(dib); dib = nullptr; }
        dibBits = nullptr;
        if (memDC) { DeleteDC(memDC); memDC = nullptr; }
        side = 0;
    }

    void ReleaseBrushes() {
        auto rel = [](ID2D1SolidColorBrush** b) {
            if (*b) { (*b)->Release(); *b = nullptr; }
        };
        rel(&fill); rel(&highlight); rel(&border); rel(&text); rel(&textSel); rel(&pointer);
        rel(&textGlow);
    }

    bool EnsureDib(int newSide) {
        if (memDC && dib && side == newSide) return true;
        ReleaseDib();
        memDC = CreateCompatibleDC(nullptr);
        if (!memDC) return false;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = newSide;
        bi.bmiHeader.biHeight = -newSide; // top-down: y grows downward
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        if (!dib || !dibBits) { ReleaseDib(); return false; }
        SelectObject(memDC, dib);
        side = newSide;
        if (!rt) return false;
        const RECT rc{ 0, 0, newSide, newSide };
        if (FAILED(rt->BindDC(memDC, &rc))) return false;
        return true;
    }
};

WheelState g;

const Action& kEmptyAction() {
    static const Action k{};
    return k;
}

// M1.1-②：按像素字号构建标签 TextFormat（11px 基准由 dpiScale 放大）。
// 失败返回 nullptr。调用方 Release。
IDWriteTextFormat* MakeTextFormat(FLOAT pxSize, const wchar_t* family = L"Segoe UI") {
    IDWriteTextFormat* tf = nullptr;
    if (!g.dwFactory ||
        FAILED(g.dwFactory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, pxSize, L"zh-Hans",
                                             &tf)) || !tf)
        return nullptr;
    tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return tf;
}

void DrawFrame() {
    if (!g.active || !g.rt || !g.memDC || !g.dib) return;

    const double tick = (double)(GetTickCount64() - g.t0);
    float scale = EaseOutCubic(Clamp01((float)(tick / kPopDurationMs)));
    if (scale <= 0.0f) scale = 0.001f;
    float alphaScale = Clamp01(scale * 1.6f);
    if (g.frame.escaped) alphaScale *= 0.35f;

    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // fully transparent base

    const int n = std::max(1, g.profile.sectorCount);
    const double cx = g.centerLocal.x, cy = g.centerLocal.y;
    const double inner = g.app.innerRadius * scale;
    const double outer = g.app.wheelRadius * scale;
    const double step = (2.0 * M_PI) / n;
    const float a = alphaScale;

    if (g.fill && g.highlight && g.border && g.text && g.textSel && g.pointer) {
        const ThemeColors th = ThemeFor(g.app.theme);
        // opacity 乘数只作用扇区填充（M3d 修正：文字不再被调淡——用户反馈
        // "低透明度下白字看不清"，文字可读性优先）。归一化 mid=基准(0.85)不变。
        const float om = Clamp01(OpacityMul(g.app.opacity) / 0.85f);
        g.fill->SetColor(D2D1::ColorF(th.fill.r, th.fill.g, th.fill.b, th.fill.a * a * om));
        g.highlight->SetColor(D2D1::ColorF(th.highlight.r, th.highlight.g, th.highlight.b,
                                           th.highlight.a * a));
        g.border->SetColor(D2D1::ColorF(th.border.r, th.border.g, th.border.b,
                                        th.border.a * a));
        g.text->SetColor(D2D1::ColorF(th.text.r, th.text.g, th.text.b, th.text.a * a));
        g.textSel->SetColor(D2D1::ColorF(th.textSel.r, th.textSel.g, th.textSel.b,
                                         th.textSel.a * a));
        g.pointer->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * a));
        // 光晕色：深色主题(白字)用暗晕、浅色主题(黑字)用亮晕
        if (g.textGlow) {
            const bool lightText = (th.text.r < 0.5f);
            g.textGlow->SetColor(lightText
                ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.75f * a)
                : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.60f * a));
        }

        const D2D1_POINT_2F center{ (FLOAT)cx, (FLOAT)cy };
        const bool drawLabels = g.app.showLabels && n <= kMaxLabelSectors && g.textFormat;
        const bool isDisc = (g.app.shape == "disc");
        // M3d 问题1：两遍渲染 —— 文字收集延后到所有填充/图标之后统一绘制，
        // 否则相邻扇区与后画的子环填充会盖住溢出扇形边界的文字矩形。
        // M3d 问题2：labelMode=selected 时只收集选中扇区/子扇区的文字。
        struct LabelReq { std::wstring text; D2D1_RECT_F box; bool sel; };
        std::vector<LabelReq> labels;
        const bool labelsAll = (g.app.labelMode != "selected");
        auto wantLabel = [&](bool sel, const std::string& name) {
            return drawLabels && !name.empty() && (labelsAll || sel);
        };
        if (g.frame.sector != g.lastSel) { g.lastSel = g.frame.sector; g.selT0 = GetTickCount64(); }
        for (int i = 0; i < n; ++i) {
            const double mid = i * step;
            const double a0 = mid - step / 2.0 + kArcGap;
            const double a1 = mid + step / 2.0 - kArcGap;
            const bool sel = (i == g.frame.sector);

            if (isDisc) {
                // ---- M3c disc：每扇区一个悬浮圆钮（无扇叶/描边/指针线）----
                const double rMid = (inner + outer) / 2.0;
                const double btnR = std::min((outer - inner) / 2.0 - 2.0 * g.dpiScale,
                                             34.0 * g.dpiScale) * scale;
                double grow = 1.0;
                if (sel) {
                    const double k = Clamp01((float)((double)(GetTickCount64() - g.selT0) / 60.0));
                    grow = 1.0 + 0.15 * EaseOutCubic((float)k);
                }
                const D2D1_POINT_2F bc{ (FLOAT)(cx + rMid * std::cos(mid)),
                                        (FLOAT)(cy + rMid * std::sin(mid)) };
                const float br = (float)(btnR * grow);
                g.rt->FillEllipse(D2D1::Ellipse(bc, br, br), sel ? g.highlight : g.fill);
                const bool hasAct = (size_t)i < g.profile.actions.size();
                const Action& act = hasAct ? g.profile.actions[i] : kEmptyAction();
                const IconRef icon = hasAct ? IconFor(act) : IconRef{};
                const bool hasGlyph = icon.isGlyph && icon.codepoint != 0;
                ID2D1Bitmap* ibmp = (!hasGlyph && !icon.file.empty())
                                        ? iconcache::Get(g.rt, icon) : nullptr;
                const bool drawGlyph = hasGlyph || (!hasGlyph && !ibmp && icon.codepoint != 0);
                const float cap = br * 0.85f;  // 图标外接不超出圆钮
                if (drawGlyph && g.glyphFormat) {
                    // 盒取整行高（1.5em）让段落居中正真居中；超出圆钮用缩放变换
                    // 收缩字形本身（裁剪盒只会切掉字形，不缩字号）。
                    const float hs = g.glyphPx * 0.75f;
                    const float fit = std::min(1.0f, cap / hs);
                    D2D1_MATRIX_3X2_F old{};
                    if (fit < 1.0f) {
                        g.rt->GetTransform(&old);
                        // D2D 行向量约定：先平移中心到原点 -> 缩放 -> 平移回去
                        D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Translation(-bc.x, -bc.y) *
                            D2D1::Matrix3x2F::Scale(fit, fit) *
                            D2D1::Matrix3x2F::Translation(bc.x, bc.y);
                        g.rt->SetTransform(old * m);
                    }
                    const D2D1_RECT_F ibox{ bc.x - hs, bc.y - hs, bc.x + hs, bc.y + hs };
                    const wchar_t ch[2] = { icon.codepoint, 0 };
                    DrawTextGlow(g.rt, ch, 1, g.glyphFormat, ibox, sel ? g.textSel : g.text, g.textGlow, 1.0f * g.dpiScale);
                    if (fit < 1.0f) g.rt->SetTransform(old);
                } else if (ibmp) {
                    const float want = (float)(g.app.iconSize * 0.5);
                    const float hs = std::min(want, cap);
                    const D2D1_RECT_F ibox{ bc.x - hs, bc.y - hs, bc.x + hs, bc.y + hs };
                    g.rt->DrawBitmap(ibmp, ibox, a);
                }
                if (hasAct && wantLabel(sel, act.name)) {
                    const float halfW = (float)(45.0 * g.dpiScale), halfH = (float)(12.0 * g.dpiScale);
                    labels.push_back(LabelReq{ Utf8ToWide(act.name),
                        D2D1::RectF(bc.x - halfW, bc.y + br + 2.0f * g.dpiScale,
                                    bc.x + halfW, bc.y + br + 2.0f * g.dpiScale + 2 * halfH), sel });
                }
                continue;
            }

            ID2D1PathGeometry* geo = BuildSectorGeometry(g.d2dFactory, center,
                                                         inner, outer, a0, a1);
            if (!geo) continue;
            g.rt->FillGeometry(geo, sel ? g.highlight : g.fill);
            g.rt->DrawGeometry(geo, g.border, (FLOAT)(1.0 * g.dpiScale));
            geo->Release();

            const bool hasAct = (size_t)i < g.profile.actions.size();
            const Action& act = hasAct ? g.profile.actions[i] : kEmptyAction();
            const IconRef icon = hasAct ? IconFor(act) : IconRef{};
            const bool hasGlyph = icon.isGlyph && icon.codepoint != 0;
            ID2D1Bitmap* ibmp = (!hasGlyph && !icon.file.empty())
                                    ? iconcache::Get(g.rt, icon) : nullptr;
            // launch 位图提取失败时，用 codepoint 里的回退字形（app/基名匹配）
            const bool useFallbackGlyph = (!hasGlyph && !ibmp && icon.codepoint != 0);
            const bool drawGlyph = hasGlyph || useFallbackGlyph;
            const bool hasIcon = drawGlyph || ibmp;
            // M3c 修正：图标恒在扇形径向正中 0.5；有图标时文字下移到 0.82，
            // 无图标文字保持 0.5。
            const double iconR = inner + (outer - inner) * 0.5;
            const FLOAT ix = (FLOAT)(cx + iconR * std::cos(mid));
            const FLOAT iy = (FLOAT)(cy + iconR * std::sin(mid));
            // M3d：图标不越扇区边界 —— 半宽受该半径处弦长约束（内缩留隙），
            // 扇区数变多/半径变化时自动缩放字形（transform 缩放，不裁剪）。
            const float halfChord = (float)std::max(6.0,
                (iconR * std::sin(step * 0.25) - 4.0) * g.dpiScale);
            if (drawGlyph && g.glyphFormat) {
                const float hs = g.glyphPx * 0.75f;   // 整行高盒保证居中
                const float fit = std::min(1.0f, halfChord / hs);
                D2D1_MATRIX_3X2_F old{};
                if (fit < 1.0f) {
                    g.rt->GetTransform(&old);
                    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Translation(-ix, -iy) *
                        D2D1::Matrix3x2F::Scale(fit, fit) *
                        D2D1::Matrix3x2F::Translation(ix, iy);
                    g.rt->SetTransform(old * m);
                }
                const D2D1_RECT_F ibox{ ix - hs, iy - hs, ix + hs, iy + hs };
                const wchar_t ch[2] = { icon.codepoint, 0 };
                DrawTextGlow(g.rt, ch, 1, g.glyphFormat, ibox, sel ? g.textSel : g.text, g.textGlow, 1.0f * g.dpiScale);
                if (fit < 1.0f) g.rt->SetTransform(old);
            } else if (ibmp) {
                const float want = (float)(g.app.iconSize * 0.5);
                const float hs = std::min(want, halfChord);
                const D2D1_RECT_F ibox{ ix - hs, iy - hs, ix + hs, iy + hs };
                g.rt->DrawBitmap(ibmp, ibox, a);
            }
            if (hasAct && wantLabel(sel, act.name)) {
                const double lr = hasIcon ? inner + (outer - inner) * 0.82
                                          : (inner + outer) / 2.0;
                const FLOAT tx = (FLOAT)(cx + lr * std::cos(mid));
                const FLOAT ty = (FLOAT)(cy + lr * std::sin(mid));
                const float halfW = (float)(45.0 * g.dpiScale), halfH = (float)(12.0 * g.dpiScale);
                labels.push_back(LabelReq{ Utf8ToWide(act.name),
                    D2D1::RectF(tx - halfW, ty - halfH, tx + halfW, ty + halfH), sel });
            }
        }

        // M3d：取消选中指针细线（用户反馈：classic 形态下多余）

        // ---- M3b 子环渲染：仅当前选中扇区、有子动作、showSub 时 ----
        if (g.frame.showSub && g.hasAnySub && g.frame.sector >= 0 &&
            g.frame.sector < (int)g.profile.actions.size()) {
            const auto& subs = g.profile.actions[g.frame.sector].subActions;
            const int m = std::min((int)subs.size(), 4);
            if (m > 0) {
                const double subTick = (double)(GetTickCount64() - g.subT0);
                const double subS = 0.6 + 0.4 * EaseOutCubic(Clamp01((float)(subTick / 100.0)));
                const double sInner = outer;  // 子环内沿 = 主环外沿
                const double sOuter = (g.app.wheelRadius + g.app.subWheelWidth * subS) * scale;
                const double mid = g.frame.sector * step;
                const double lo = mid - step / 2.0 + kArcGap;
                const double hi = mid + step / 2.0 - kArcGap;
                const double span = (hi - lo) / m;
                for (int j = 0; j < m; ++j) {
                    const double a0 = lo + span * j + kArcGap * 0.5;
                    const double a1 = lo + span * (j + 1) - kArcGap * 0.5;
                    if (a1 <= a0) continue;
                    const bool sel = (j == g.frame.subSector);
                    if (isDisc) {
                        const double smid = (a0 + a1) / 2.0;
                        const double rMid = (sInner + sOuter) / 2.0;
                        const D2D1_POINT_2F bc{ (FLOAT)(cx + rMid * std::cos(smid)),
                                                (FLOAT)(cy + rMid * std::sin(smid)) };
                        const float br = (float)std::min((sOuter - sInner) / 2.0 - 2.0 * g.dpiScale,
                                                         24.0 * g.dpiScale) * scale;
                        g.rt->FillEllipse(D2D1::Ellipse(bc, br * (sel ? 1.15f : 1.0f),
                                                        br * (sel ? 1.15f : 1.0f)),
                                          sel ? g.highlight : g.fill);
                    } else {
                    ID2D1PathGeometry* geo = BuildSectorGeometry(g.d2dFactory, center,
                                                                 sInner, sOuter, a0, a1);
                    if (!geo) continue;
                    g.rt->FillGeometry(geo, sel ? g.highlight : g.fill);
                    g.rt->DrawGeometry(geo, g.border, (FLOAT)(1.0 * g.dpiScale));
                    geo->Release();
                    }
                    {
                        const double smid = (a0 + a1) / 2.0;
                        const double lr = (sInner + sOuter) / 2.0;
                        const FLOAT sx = (FLOAT)(cx + lr * std::cos(smid));
                        const FLOAT sy = (FLOAT)(cy + lr * std::sin(smid));
                        const IconRef sicon = IconFor(subs[j]);
                        ID2D1Bitmap* sbmp = (!sicon.isGlyph || !sicon.codepoint) && !sicon.file.empty()
                                                ? iconcache::Get(g.rt, sicon) : nullptr;
                        if (sbmp) {
                            const float hs = (float)(std::min(g.app.iconSize,
                                                              g.app.subWheelWidth * 0.6) * 0.5);
                            const D2D1_RECT_F ibox{ sx - hs, sy - hs, sx + hs, sy + hs };
                            g.rt->DrawBitmap(sbmp, ibox, a);
                        } else if (sicon.isGlyph && sicon.codepoint && g.glyphFormatSub) {
                            const float hs = g.glyphSubPx * 0.6f;
                            const D2D1_RECT_F ibox{ sx - hs, sy - hs, sx + hs, sy + hs };
                            const wchar_t ch[2] = { sicon.codepoint, 0 };
                            DrawTextGlow(g.rt, ch, 1, g.glyphFormatSub, ibox, sel ? g.textSel : g.text, g.textGlow, 1.0f * g.dpiScale);
                        } else if (wantLabel(sel, subs[j].name)) {
                            const float halfW = (float)(45.0 * g.dpiScale), halfH = (float)(12.0 * g.dpiScale);
                            labels.push_back(LabelReq{ Utf8ToWide(subs[j].name),
                                D2D1::RectF(sx - halfW, sy - halfH, sx + halfW, sy + halfH), sel });
                        }
                    }
                }
            }
        }

        // ---- pass 2：文字永远画在最上层（含子环填充之上）----
        for (const LabelReq& lr : labels)
            DrawTextGlow(g.rt, lr.text.c_str(), (UINT32)lr.text.size(), g.textFormat,
                         lr.box, lr.sel ? g.textSel : g.text, g.textGlow, 1.0f * g.dpiScale);
    }

    if (FAILED(g.rt->EndDraw())) return; // device lost / size mismatch — skip present

    SIZE sz{ g.side, g.side };
    POINT ptSrc{ 0, 0 };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g.hwnd, nullptr, &g.origin, &sz, g.memDC, &ptSrc, 0, &bf, ULW_ALPHA);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (wp == kTimerId) DrawFrame();
        return 0;
    case WM_NCHITTEST: {
        // 手势激活期间，圆盘范围内吸收鼠标（HTCLIENT）：下层窗口不再收到
        // hover/点击，修复"选扇区时鼠标穿轮盘碰到后面窗口"。圆盘外与
        // 空闲期一律 HTTRANSPARENT 穿透（钩子收事件不受命中测试影响）。
        if (g.active) {
            const POINT pt{ (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
            const double dx = pt.x - (g.origin.x + g.centerLocal.x);
            const double dy = pt.y - (g.origin.y + g.centerLocal.y);
            // M3b：子环展开时吸收半径扩到子环外沿
            const double r = (g.frame.showSub && g.hasAnySub
                                  ? g.app.wheelRadius + g.app.subWheelWidth
                                  : g.app.wheelRadius) + 6.0;
            if (dx * dx + dy * dy <= r * r) return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_ERASEBKGND:
        return 1; // layered window: never erase
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

namespace WheelWindow {

bool Create(HINSTANCE hi) {
    g.hinst = hi;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g.d2dFactory)) ||
        !g.d2dFactory)
        return false;

    // 软件光栅器：避免默认硬件 RT 创建 D3D 设备（实测占私有提交 ~50MB，
    // 直接击穿「空闲工作集 < 8MB」验收线）。DC+DIB 路径下软件 RT 完全兼容，
    // 300x300 轮盘 120fps 重绘 CPU 开销可忽略。
    const D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                       D2D1_ALPHA_MODE_PREMULTIPLIED),
                                     0, 0);
    if (FAILED(g.d2dFactory->CreateDCRenderTarget(&props, &g.rt)) || !g.rt)
        return false;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g.dwFactory))) ||
        !g.dwFactory)
        return false;

    g.textFormat = MakeTextFormat(11.0f);
    if (!g.textFormat)
        return false;
    g.glyphFormat = MakeTextFormat(28.0f, L"Segoe MDL2 Assets");
    if (!g.glyphFormat)
        return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.lpszClassName = L"YiPieWheelWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    // M2 修复：不再用 WS_EX_TRANSPARENT（那会在命中测试前无条件穿透，
    // 导致手势期间 hover/点击穿过轮盘打到后面窗口）。穿透改由
    // WM_NCHITTEST 逐点决定：轮盘激活且光标在圆盘内 -> HTCLIENT 吸收；
    // 其余一切情况 -> HTTRANSPARENT（Windows 视为不存在，消息落到下层窗口）。
    g.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED |
                                 WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             L"YiPieWheelWindow", L"", WS_POPUP, 0, 0, 1, 1,
                             nullptr, nullptr, hi, nullptr);
    if (!g.hwnd) return false;
    ShowWindow(g.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void SetDpiScale(double scale) {
    if (scale <= 0.0) scale = 1.0;
    if (g.dpiScale == scale || !g.dwFactory) return;
    g.dpiScale = scale;
    // 字号随 DPI 重建（几何半径由宿主缩放后随 Show 传入）
    IDWriteTextFormat* tf = MakeTextFormat((FLOAT)(11.0 * scale));
    if (tf) {
        if (g.textFormat) g.textFormat->Release();
        g.textFormat = tf;
    }
    // M3c：iconSize 由宿主按 DPI 缩放后传入，字形字号直接用它（Show 里重建）；
    // 这里只兜底重建一次默认尺寸。
    IDWriteTextFormat* gf = MakeTextFormat((FLOAT)g.app.iconSize, L"Segoe MDL2 Assets");
    if (gf) {
        if (g.glyphFormat) g.glyphFormat->Release();
        g.glyphFormat = gf;
    }
}

void Show(double cx, double cy, const Profile& profile, const AppearanceSettings& app) {
    if (!g.hwnd || !g.rt) return;
    g.profile = profile;
    g.app = app;

    // M3b：任一扇区有子动作时，窗口预留子环空间（side 覆盖 subOuter+margin）。
    g.hasAnySub = false;
    for (const Action& a : profile.actions)
        if (!a.subActions.empty()) { g.hasAnySub = true; break; }
    const double outerDraw = g.hasAnySub ? app.wheelRadius + app.subWheelWidth : app.wheelRadius;
    const int side = 2 * (int)std::ceil(outerDraw + kShadowMargin);
    if (!g.EnsureDib(side)) return;

    g.centerLocal = POINT{ side / 2, side / 2 };
    g.origin = POINT{ (LONG)std::llround(cx - side / 2.0),
                      (LONG)std::llround(cy - side / 2.0) };
    SetWindowPos(g.hwnd, HWND_TOPMOST, g.origin.x, g.origin.y, side, side,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    g.ReleaseBrushes();
    const ThemeColors th = ThemeFor(app.theme);
    auto mk = [&](D2D1_COLOR_F c, ID2D1SolidColorBrush** out) {
        return SUCCEEDED(g.rt->CreateSolidColorBrush(c, out)) && *out != nullptr;
    };
    if (!mk(th.fill, &g.fill) || !mk(th.highlight, &g.highlight) ||
        !mk(th.border, &g.border) || !mk(th.text, &g.text) ||
        !mk(th.textSel, &g.textSel) || !mk(D2D1::ColorF(1, 1, 1, 1), &g.pointer) ||
        !mk(D2D1::ColorF(0, 0, 0, 0.6f), &g.textGlow)) {
        g.ReleaseBrushes();
        return;
    }

    // M3c：图标字号跟随 appearance.iconSize（宿主已 DPI 缩放）；子环封顶
    // = 子环宽度*0.6，避免 16 扇区+窄子环时字形溢出扇叶。
    {
        const float want = (float)std::max(8.0, app.iconSize);
        if (fabsf(want - g.glyphPx) > 0.5f || !g.glyphFormat) {
            IDWriteTextFormat* gf = MakeTextFormat(want, L"Segoe MDL2 Assets");
            if (gf) {
                if (g.glyphFormat) g.glyphFormat->Release();
                g.glyphFormat = gf;
                g.glyphPx = want;
            }
        }
        const float subWant = (float)std::max(8.0, std::min(app.iconSize, app.subWheelWidth * 0.6));
        if (fabsf(subWant - g.glyphSubPx) > 0.5f || !g.glyphFormatSub) {
            IDWriteTextFormat* gf = MakeTextFormat(subWant, L"Segoe MDL2 Assets");
            if (gf) {
                if (g.glyphFormatSub) g.glyphFormatSub->Release();
                g.glyphFormatSub = gf;
                g.glyphSubPx = subWant;
            }
        }
    }
    g.frame = Frame{}; // fresh activation: no selection, not escaped
    g.t0 = GetTickCount64();
    g.active = true;
    SetTimer(g.hwnd, kTimerId, kTimerMs, nullptr);
    DrawFrame(); // immediate first frame
}

void Update(const Frame& f) {
    if (!g.active) return;
    if (f.showSub && !g.frame.showSub) g.subT0 = GetTickCount64();  // 展开动画起表
    g.frame = f; // latest frame wins; WM_TIMER repaints within ~8ms
}

void Hide() {
    if (!g.hwnd) return;
    g.active = false;
    KillTimer(g.hwnd, kTimerId);
    g.ReleaseBrushes();
    ShowWindow(g.hwnd, SW_HIDE);
}

bool IsActive() { return g.active; }

} // namespace WheelWindow

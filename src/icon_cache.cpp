#include "icon_cache.h"
#include "icon_extract.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <d2d1.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <list>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace {

constexpr int kIconPx = 128;   // 高分辨率提取，扇区内缩小显示仍清晰（问题2画质修复）
constexpr size_t kCacheCap = 32;   // 128px 单张 64KB，32 张 ~2MB（原 64 张会到 4MB）

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::wstring IconsDir() {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base))) return {};
    return std::wstring(base) + L"\\YiPie\\icons";
}

std::wstring Utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

bool HasPathSep(const std::string& f) {
    return f.find('\\') != std::string::npos || f.find('/') != std::string::npos;
}
bool IsExeLike(const std::string& f) {
    std::string l = Lower(f);
    return l.size() > 4 && (l.compare(l.size() - 4, 4, ".exe") == 0 ||
                            l.compare(l.size() - 4, 4, ".lnk") == 0 ||
                            l.compare(l.size() - 4, 4, ".url") == 0);
}

// 把 64x64 BGRA 缓冲（straight alpha）预乘后建成 D2D 位图。
// 无 alpha 通道（全 0）时按"非黑像素=不透明"兜底（GDI 掩码绘制的经典图标）。
ID2D1Bitmap* BitmapFromBuffer(ID2D1RenderTarget* rt, const uint8_t* bgra) {
    static uint8_t buf[kIconPx * kIconPx * 4];
    std::copy_n(bgra, sizeof(buf), buf);
    bool anyAlpha = false;
    for (int i = 3; i < (int)sizeof(buf); i += 4)
        if (buf[i]) { anyAlpha = true; break; }
    if (!anyAlpha) {
        for (int i = 0; i < (int)sizeof(buf); i += 4)
            buf[i + 3] = (buf[i] | buf[i + 1] | buf[i + 2]) ? 255 : 0;
    } else {
        for (int i = 0; i < (int)sizeof(buf); i += 4) {
            const uint32_t a = buf[i + 3];
            buf[i + 0] = (uint8_t)(buf[i + 0] * a / 255);
            buf[i + 1] = (uint8_t)(buf[i + 1] * a / 255);
            buf[i + 2] = (uint8_t)(buf[i + 2] * a / 255);
        }
    }
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1Bitmap* bmp = nullptr;
    if (FAILED(rt->CreateBitmap(D2D1::SizeU(kIconPx, kIconPx), buf, kIconPx * 4, props, &bmp)))
        return nullptr;
    return bmp;
}

ID2D1Bitmap* ExtractExeIcon(ID2D1RenderTarget* rt, const std::wstring& path) {
    // 共享提取器：解析快捷方式目标 + 高分辨率 + 无箭头角标（问题2）
    HICON hIcon = ExtractCleanIcon(path, kIconPx);
    if (!hIcon) return nullptr;
    ID2D1Bitmap* result = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kIconPx;
    bi.bmiHeader.biHeight = -kIconPx;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        memset(bits, 0, kIconPx * kIconPx * 4);
        HGDIOBJ old = SelectObject(memDC, dib);
        DrawIconEx(memDC, 0, 0, hIcon, kIconPx, kIconPx, 0, nullptr, DI_NORMAL);
        SelectObject(memDC, old);
        result = BitmapFromBuffer(rt, (const uint8_t*)bits);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screen);
    DestroyIcon(hIcon);
    return result;
}

IWICImagingFactory* WicFactory() {
    static IWICImagingFactory* f = [] {
        IWICImagingFactory* p = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&p));
        return p;
    }();
    return f;
}

ID2D1Bitmap* DecodePng(ID2D1RenderTarget* rt, const std::wstring& path) {
    IWICImagingFactory* wic = WicFactory();
    if (!wic) return nullptr;
    ID2D1Bitmap* result = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnLoad, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)) ||
        FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom))) {
    } else {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        std::vector<uint8_t> buf((size_t)w * h * 4);
        if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)buf.size(), buf.data()))) {
            // 非 64 方图：缩放到 64x64 再入缓存（简单盒采样足够，图标小）
            std::vector<uint8_t> out64(kIconPx * kIconPx * 4, 0);
            for (int y = 0; y < kIconPx; y++)
                for (int x = 0; x < kIconPx; x++) {
                    const int sx = x * (int)w / kIconPx, sy = y * (int)h / kIconPx;
                    memcpy(&out64[(y * kIconPx + x) * 4], &buf[((size_t)sy * w + sx) * 4], 4);
                }
            result = BitmapFromBuffer(rt, out64.data());
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    return result;
}

std::wstring MtimeTag(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return L"0";
    wchar_t buf[40];
    swprintf(buf, 40, L"%08lx%08lx", fad.ftLastWriteTime.dwHighDateTime,
             fad.ftLastWriteTime.dwLowDateTime);
    return buf;
}

struct Entry {
    ID2D1Bitmap* bmp = nullptr;
    std::list<std::string>::iterator lruIt;
};

std::unordered_map<std::string, Entry> g_cache;
std::list<std::string> g_lru;   // front = 最近使用

}  // namespace

namespace iconcache {

ID2D1Bitmap* Get(ID2D1RenderTarget* rt, const IconRef& ref) {
    if (!rt || ref.file.empty()) return nullptr;

    std::wstring full;
    std::string key;
    if (HasPathSep(ref.file) || IsExeLike(ref.file)) {
        full = Utf8ToWideLocal(ref.file);
        key = "e|" + Lower(ref.file);
    } else {
        const std::wstring dir = IconsDir();
        if (dir.empty()) return nullptr;
        full = dir + L"\\" + Utf8ToWideLocal(ref.file);
        key = "f|" + Lower(ref.file);
    }
    key += "|" + [&] { std::string w; auto t = MtimeTag(full);
                       for (wchar_t c : t) w += (char)c; return w; }();

    auto it = g_cache.find(key);
    if (it != g_cache.end()) {
        g_lru.splice(g_lru.begin(), g_lru, it->second.lruIt);  // touch
        return it->second.bmp;
    }

    ID2D1Bitmap* bmp = (key[0] == 'e') ? ExtractExeIcon(rt, full)
                                       : DecodePng(rt, full);
    if (!bmp) return nullptr;

    if (g_cache.size() >= kCacheCap) {
        const std::string victim = g_lru.back();
        g_lru.pop_back();
        auto vit = g_cache.find(victim);
        if (vit != g_cache.end()) {
            if (vit->second.bmp) vit->second.bmp->Release();
            g_cache.erase(vit);
        }
    }
    g_lru.push_front(key);
    g_cache.emplace(std::move(key), Entry{bmp, g_lru.begin()});
    return bmp;
}

void Clear() {
    for (auto& [k, e] : g_cache)
        if (e.bmp) e.bmp->Release();
    g_cache.clear();
    g_lru.clear();
}

}  // namespace iconcache

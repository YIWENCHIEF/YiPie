#include "icon_lookup.h"
#include "glyph_table.generated.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "C:\a\b\chrome.exe" -> "chrome.exe"；'/' 也当分隔符（URL 场景）
std::string BaseName(const std::string& p) {
    const size_t sep = p.find_last_of("\\/");
    return sep == std::string::npos ? p : p.substr(sep + 1);
}

IconRef GlyphRef(const char* id) {
    IconRef r;
    r.isGlyph = true;
    r.codepoint = GlyphCp(id);
    return r;
}

}  // namespace

wchar_t GlyphCp(const char* id) {
    for (size_t i = 0; i < kGlyphCount; ++i)
        if (std::strcmp(kGlyphs[i].id, id) == 0) return kGlyphs[i].cp;
    return 0;
}

IconRef IconFor(const Action& a) {
    // 1) 手动值（M3a 归一化保证 ""/g:/f: 三态）
    if (a.iconKey.rfind("g:", 0) == 0) {
        IconRef r;
        r.isGlyph = true;
        r.codepoint = GlyphCp(a.iconKey.c_str() + 2);
        if (!r.codepoint) { r.isGlyph = false; return GlyphRef("app"); }
        return r;
    }
    if (a.iconKey.rfind("f:", 0) == 0) {
        IconRef r;
        r.file = a.iconKey.substr(2);
        return r;
    }
    if (a.type.empty()) return IconRef{};

    const std::string t = Lower(a.target);

    // launch（M3d 修正）：优先提取 exe 自身位图图标（file=target），
    // codepoint 存回退字形（基名 patterns -> app），供位图失败时使用。
    if (a.type == "launch") {
        IconRef r;
        r.isGlyph = false;
        r.file = a.target;
        const std::string base = BaseName(t);
        std::string stem = base;
        const size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
        for (size_t i = 0; i < kGlyphCount && r.codepoint == 0; ++i)
            for (const char* const* p = kGlyphs[i].patterns; *p; ++p)
                if (base == *p || stem == *p) { r.codepoint = kGlyphs[i].cp; break; }
        if (!r.codepoint) r.codepoint = GlyphCp("app");
        return r;
    }

    // hotkey：patterns 精确匹配 -> keyboard 兜底
    for (size_t i = 0; i < kGlyphCount; ++i)
        for (const char* const* p = kGlyphs[i].patterns; *p; ++p)
            if (t == *p) { IconRef r; r.isGlyph = true; r.codepoint = kGlyphs[i].cp; return r; }

    // 4) hotkey 未命中语义表：keyboard 字形（比 app 更贴切）
    if (a.type == "hotkey") return GlyphRef("keyboard");
    return GlyphRef("app");
}

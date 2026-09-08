#include "doctest.h"
#include "icon_lookup.h"
#include <cstring>

TEST_CASE("IconFor: iconKey 手动值优先") {
    Action a;
    a.type = "hotkey"; a.target = "ctrl+c";
    a.iconKey = "g:save";
    IconRef r = IconFor(a);
    CHECK(r.isGlyph);
    CHECK(r.codepoint == 0xE74E);   // save
    CHECK(r.file.empty());

    a.iconKey = "f:my.png";
    r = IconFor(a);
    CHECK_FALSE(r.isGlyph);
    CHECK(r.file == "my.png");
}

TEST_CASE("IconFor: hotkey 语义匹配") {
    Action a; a.type = "hotkey"; a.iconKey = "";
    a.target = "ctrl+c";
    IconRef r = IconFor(a);
    CHECK(r.isGlyph);
    CHECK(r.codepoint == 0xE8C8);   // copy

    a.target = "alt+tab";
    r = IconFor(a);
    CHECK(r.isGlyph);
    CHECK(r.codepoint == 0xE78B);   // window

    a.target = "win+shift+s";
    r = IconFor(a);
    CHECK(r.codepoint == 0xE722);   // camera（截图专属，search 已让位）
}

TEST_CASE("IconFor: launch 优先位图（file=target），回退字形存 codepoint") {
    Action a; a.type = "launch"; a.iconKey = "";
    a.target = "C:\\Program Files\\Google\\Chrome\\chrome.exe";
    IconRef r = IconFor(a);
    CHECK_FALSE(r.isGlyph);                    // 位图路径
    CHECK(r.file == a.target);                 // 提取目标
    CHECK(r.codepoint == 0xE774);              // 回退字形 browser

    a.target = "C:\\Windows\\System32\\calc.exe";
    r = IconFor(a);
    CHECK_FALSE(r.isGlyph);
    CHECK(r.codepoint == 0xE8EF);              // calc

    a.target = "C:\\some\\unknown_app.exe";
    r = IconFor(a);
    CHECK_FALSE(r.isGlyph);
    CHECK(r.codepoint == 0xECAA);              // app 兜底
}

TEST_CASE("IconFor: 空槽返回空 ref") {
    Action a; a.type = "";
    IconRef r = IconFor(a);
    CHECK_FALSE(r.isGlyph);
    CHECK(r.file.empty());
    CHECK(r.codepoint == 0);
}

TEST_CASE("GlyphCp: 按 id 查码点") {
    CHECK(GlyphCp("copy") == 0xE8C8);
    CHECK(GlyphCp("nope") == 0);
}

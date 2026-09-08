#include "doctest.h"
#include "hotkey_parser.h"
#include <windows.h>   // 仅取 VK_* 常量与字符字面量, 解析器本身不调任何 API

TEST_CASE("常规组合键") {
    HotkeySpec s; std::string err;
    REQUIRE(ParseHotkey("ctrl+alt+t", s, err));
    REQUIRE(s.vkOrder.size() == 3);
    CHECK(s.vkOrder[0] == VK_CONTROL);
    CHECK(s.vkOrder[1] == VK_MENU);
    CHECK(s.vkOrder[2] == 'T');
}

TEST_CASE("顺序归一: 主键永远最后, 修饰键按 ctrl>alt>shift>win 固定排") {
    HotkeySpec s; std::string err;
    REQUIRE(ParseHotkey("t+shift", s, err));
    CHECK(s.vkOrder == std::vector<uint16_t>{VK_SHIFT, 'T'});
    REQUIRE(ParseHotkey("win+d", s, err));
    CHECK(s.vkOrder == std::vector<uint16_t>{VK_LWIN, 'D'});
}

TEST_CASE("特殊键名与大小写/空格容错") {
    HotkeySpec s; std::string err;
    REQUIRE(ParseHotkey("  Ctrl + Tab ", s, err));   CHECK(s.vkOrder[1] == VK_TAB);
    REQUIRE(ParseHotkey("alt+F4", s, err));          CHECK(s.vkOrder[1] == VK_F4);
    REQUIRE(ParseHotkey("ctrl+PageDown", s, err));   CHECK(s.vkOrder[1] == VK_NEXT);
    REQUIRE(ParseHotkey("shift+delete", s, err));    CHECK(s.vkOrder[1] == VK_DELETE);
    REQUIRE(ParseHotkey("ctrl+shift+win+alt+1", s, err)); CHECK(s.vkOrder[4] == '1');
}

TEST_CASE("纯修饰键组合") {
    HotkeySpec s; std::string err;
    REQUIRE(ParseHotkey("shift+alt", s, err));
    CHECK(s.vkOrder == std::vector<uint16_t>{VK_MENU, VK_SHIFT}); // 固定序 ctrl,alt,shift,win
}

TEST_CASE("非法输入") {
    HotkeySpec s; std::string err;
    CHECK_FALSE(ParseHotkey("", s, err));
    CHECK_FALSE(ParseHotkey("ctrl+", s, err));
    CHECK_FALSE(ParseHotkey("ctrl+banana", s, err));
}

TEST_CASE("扩展键标志") {
    CHECK(IsExtendedVk(VK_UP)); CHECK(IsExtendedVk(VK_DELETE)); CHECK(IsExtendedVk(VK_LWIN));
    CHECK_FALSE(IsExtendedVk('A')); CHECK_FALSE(IsExtendedVk(VK_CONTROL));
}

TEST_CASE("M3d: 音量/媒体键可解析（预设触发修复）") {
    HotkeySpec sp; std::string err;
    REQUIRE(ParseHotkey("volumeup", sp, err));
    CHECK(sp.vkOrder.size() == 1);
    CHECK(sp.vkOrder[0] == 0xAF);
    REQUIRE(ParseHotkey("volumemute", sp, err));
    CHECK(sp.vkOrder[0] == 0xAD);
    CHECK(IsExtendedVk(0xAF));          // 媒体键须为扩展键
    CHECK(IsExtendedVk(0xAD));
    REQUIRE(ParseHotkey("ctrl+alt+delete", sp, err));
    CHECK(sp.vkOrder.size() == 3);
}

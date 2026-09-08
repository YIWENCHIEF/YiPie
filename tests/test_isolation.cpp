#include "doctest.h"
#include "isolation.h"

TEST_CASE("Isolated: 黑名单模式——在列则隔离") {
    std::vector<std::string> bl = {"chrome.exe", "game.exe"};
    CHECK(Isolated("blacklist", bl, "chrome.exe"));
    CHECK_FALSE(Isolated("blacklist", bl, "notepad.exe"));
    CHECK_FALSE(Isolated("blacklist", {}, "anything.exe"));  // 空黑名单=全放行
}

TEST_CASE("Isolated: 白名单模式——不在列则隔离") {
    std::vector<std::string> wl = {"notepad.exe"};
    CHECK_FALSE(Isolated("whitelist", wl, "notepad.exe"));   // 在白名单=放行
    CHECK(Isolated("whitelist", wl, "chrome.exe"));          // 不在=隔离
    CHECK(Isolated("whitelist", {}, "anything.exe"));        // 空白名单=全隔离
}

TEST_CASE("Isolated: 未知模式按黑名单") {
    std::vector<std::string> list = {"a.exe"};
    CHECK(Isolated("garbage", list, "a.exe"));
    CHECK_FALSE(Isolated("garbage", list, "b.exe"));
}

TEST_CASE("Isolated: 空 procName 不误伤") {
    CHECK_FALSE(Isolated("blacklist", {"a.exe"}, ""));   // 黑名单：空名不在列 -> 放行
    CHECK(Isolated("whitelist", {"a.exe"}, ""));          // 白名单：空名不在列 -> 隔离
}

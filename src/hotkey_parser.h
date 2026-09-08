#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct HotkeySpec {
    std::vector<uint16_t> vkOrder;  // 按下顺序(修饰键在前), 释放逆序。全部为 VK_*
};

// "ctrl+shift+t" -> {VK_CONTROL, VK_SHIFT, 'T'}; 大小写不敏感, 忽略空格
// 纯修饰键组合("shift+alt")也合法。非法 token 返回 false + err。
bool ParseHotkey(const std::string& text, HotkeySpec& out, std::string& err);
// SendInput 是否需要 KEYEVENTF_EXTENDEDKEY
bool IsExtendedVk(uint16_t vk);

// Task 3: 热键字符串解析（纯逻辑, 仅使用 windows.h 的 VK_* 常量, 不调用任何 Win32 API）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // 与 config.cpp 一致: 避免 windows.h 的 min/max 宏污染
#endif
#include <windows.h>

#include "hotkey_parser.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace {

std::string TrimLower(const std::string& raw) {
    size_t begin = 0, end = raw.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) --end;
    std::string out;
    out.reserve(end - begin);
    for (size_t i = begin; i < end; ++i)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i]))));
    return out;
}

enum class KeyKind { Modifier, Main };

struct KeyInfo {
    KeyKind kind;
    uint16_t vk;
};

// 修饰键表: 输出固定顺序为 ctrl, alt, shift, win(=VK_LWIN)
const std::map<std::string, uint16_t>& ModifierTable() {
    static const std::map<std::string, uint16_t> table = {
        {"ctrl", VK_CONTROL},    {"control", VK_CONTROL},
        {"alt", VK_MENU},        {"menu", VK_MENU},
        {"shift", VK_SHIFT},
        {"win", VK_LWIN},        {"windows", VK_LWIN}, {"super", VK_LWIN}, {"cmd", VK_LWIN},
    };
    return table;
}

const std::map<std::string, uint16_t>& SpecialKeyTable() {
    static const std::map<std::string, uint16_t> table = {
        {"tab", VK_TAB},
        {"esc", VK_ESCAPE},      {"escape", VK_ESCAPE},
        {"enter", VK_RETURN},    {"return", VK_RETURN},
        {"space", VK_SPACE},
        {"backspace", VK_BACK},  {"bksp", VK_BACK},
        {"delete", VK_DELETE},   {"del", VK_DELETE},
        {"insert", VK_INSERT},   {"ins", VK_INSERT},
        {"home", VK_HOME},
        {"end", VK_END},
        {"pageup", VK_PRIOR},    {"pgup", VK_PRIOR},
        {"pagedown", VK_NEXT},   {"pgdn", VK_NEXT},
        {"printscreen", VK_SNAPSHOT}, {"prtsc", VK_SNAPSHOT},
        {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
        {"f1", VK_F1},   {"f2", VK_F2},   {"f3", VK_F3},     {"f4", VK_F4},
        {"f5", VK_F5},   {"f6", VK_F6},   {"f7", VK_F7},     {"f8", VK_F8},
        {"f9", VK_F9},   {"f10", VK_F10}, {"f11", VK_F11},   {"f12", VK_F12},
        {"f13", VK_F1 + 12}, {"f14", VK_F1 + 13}, {"f15", VK_F1 + 14}, {"f16", VK_F1 + 15},
        {"f17", VK_F1 + 16}, {"f18", VK_F1 + 17}, {"f19", VK_F1 + 18}, {"f20", VK_F1 + 19},
        {"f21", VK_F1 + 20}, {"f22", VK_F1 + 21}, {"f23", VK_F1 + 22}, {"f24", VK_F1 + 23},
        // M3d：音量/媒体键（0xAD~0xB6，注入时需 KEYEVENTF_EXTENDEDKEY）
        {"volumemute", 0xAD}, {"volumedown", 0xAE}, {"volumeup", 0xAF},
        {"nexttrack", 0xB0}, {"prevtrack", 0xB6}, {"playpause", 0xB3},
        {"stoptrack", 0xB5},
    };
    return table;
}

// 解析单个 token -> VK。失败返回 false。
bool TokenToVk(const std::string& tok, KeyInfo& info) {
    if (tok.empty()) return false;

    auto modIt = ModifierTable().find(tok);
    if (modIt != ModifierTable().end()) {
        info = {KeyKind::Modifier, modIt->second};
        return true;
    }

    auto specialIt = SpecialKeyTable().find(tok);
    if (specialIt != SpecialKeyTable().end()) {
        info = {KeyKind::Main, specialIt->second};
        return true;
    }

    if (tok.size() == 1) {
        char c = tok[0];
        if (c >= 'a' && c <= 'z') {
            info = {KeyKind::Main, static_cast<uint16_t>('A' + (c - 'a'))};
            return true;
        }
        if (c >= '0' && c <= '9') {
            info = {KeyKind::Main, static_cast<uint16_t>(c)};  // VK_0..VK_9 即 ASCII '0'..'9'
            return true;
        }
    }

    return false;
}

}  // namespace

bool ParseHotkey(const std::string& text, HotkeySpec& out, std::string& err) {
    out.vkOrder.clear();
    err.clear();

    if (TrimLower(text).empty()) {
        err = "hotkey is empty";
        return false;
    }

    // 固定输出顺序: ctrl, alt, shift, win。按 VK 值记录存在性(天然去重)。
    uint16_t modifierOrder[4] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN};
    bool modifierSeen[4] = {false, false, false, false};
    bool hasMain = false;
    uint16_t mainVk = 0;

    size_t start = 0;
    while (start <= text.size()) {
        size_t plus = text.find('+', start);
        if (plus == std::string::npos) plus = text.size();
        const std::string tok = TrimLower(text.substr(start, plus - start));
        if (tok.empty()) {
            err = "empty key token in hotkey";
            return false;
        }

        KeyInfo info;
        if (!TokenToVk(tok, info)) {
            err = "unknown key token: '" + tok + "'";
            return false;
        }

        if (info.kind == KeyKind::Modifier) {
            for (int i = 0; i < 4; ++i)
                if (modifierOrder[i] == info.vk) modifierSeen[i] = true;
        } else {
            if (hasMain) {
                err = "hotkey has two main keys";
                return false;
            }
            hasMain = true;
            mainVk = info.vk;
        }

        if (plus == text.size()) break;
        start = plus + 1;
    }

    for (int i = 0; i < 4; ++i)
        if (modifierSeen[i]) out.vkOrder.push_back(modifierOrder[i]);
    if (hasMain) out.vkOrder.push_back(mainVk);

    if (out.vkOrder.empty()) {  // 理论上不可达(空串已提前拦截), 防御性兜底
        err = "hotkey resolved to no keys";
        return false;
    }
    return true;
}

bool IsExtendedVk(uint16_t vk) {
    // 音量/媒体键均为扩展键
    if (vk >= 0xAD && vk <= 0xB6) return true;
    switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_INSERT: case VK_DELETE:
        case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT:
        case VK_SNAPSHOT: case VK_NUMLOCK: case VK_DIVIDE:
        case VK_RCONTROL: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

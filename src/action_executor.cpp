#include "action_executor.h"
#include "hotkey_parser.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <vector>

namespace {
uint16_t ScanOf(uint16_t vk) {
    return static_cast<uint16_t>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
}
INPUT Key(uint16_t vk, bool down) {
    INPUT in{}; in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = ScanOf(vk);
    in.ki.dwFlags = (down ? 0u : KEYEVENTF_KEYUP);
    if (vk >= 0xE000) in.ki.dwFlags |= KEYEVENTF_UNICODE; // 本 M1 不触发, 留扩展位
    if (IsExtendedVk(vk)) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    in.ki.time = 0; in.ki.dwExtraInfo = 0;
    return in;
}
}

namespace ActionExecutor {

bool SimulateHotkey(const std::string& text, std::string* err) {
    HotkeySpec spec;
    std::string werr;
    // 轮盘刚松手, 先等宿主应用处理完"键抬起"上下文再注入
    Sleep(15);
    if (!ParseHotkey(text, spec, werr)) { if (err) *err = werr; return false; }
    std::vector<INPUT> inputs;
    for (uint16_t vk : spec.vkOrder) inputs.push_back(Key(vk, true));
    // 修饰键全部按下后保持 15ms 再敲主键, 避免被宿主当作裸键(实测验证过的修正)
    SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    Sleep(15);
    INPUT ev[2];
    int n = 0;
    ev[n] = Key(spec.vkOrder.back(), true); n++;
    ev[n] = Key(spec.vkOrder.back(), false); n++;
    SendInput(n, ev, sizeof(INPUT));
    Sleep(15);
    std::vector<INPUT> ups;
    for (auto it = spec.vkOrder.rbegin(); it != spec.vkOrder.rend(); ++it)
        ups.push_back(Key(*it, false));
    return SendInput((UINT)ups.size(), ups.data(), sizeof(INPUT)) == (UINT)ups.size();
}

bool Launch(const std::string& target, const std::string& args) {
    auto wtarget = std::wstring(target.begin(), target.end());
    auto wargs = std::wstring(args.begin(), args.end());
    auto h = ShellExecuteW(nullptr, L"open", wtarget.c_str(), wargs.empty() ? nullptr : wargs.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(h) > 32;
}

bool Execute(const Action& a, std::string* err) {
    if (a.type == "launch") {
        // 空 target 会被 ShellExecuteW 当成"打开默认位置"（弹资源管理器），必须拒绝
        if (a.target.empty()) {
            if (err) *err = "launch target is empty";
            return false;
        }
        return Launch(a.target, a.args);
    }
    if (a.type == "hotkey") {
        if (a.target.empty()) {
            if (err) *err = "hotkey is empty";
            return false;
        }
        return SimulateHotkey(a.target, err);
    }
    return a.type.empty();
}
} // namespace ActionExecutor

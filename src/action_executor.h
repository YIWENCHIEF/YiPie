#pragma once
#include "config.h"
#include <string>

namespace ActionExecutor {
// 统一入口: type=="launch" -> ShellExecuteW(L"open", target, args); type=="hotkey" -> SimulateHotkey
// 空 type 返回 true(无事发生)。在 UI 线程调用安全(含 Sleep)。
bool Execute(const Action& a, std::string* err);
bool Launch(const std::string& target, const std::string& args);
bool SimulateHotkey(const std::string& hotkeyText, std::string* err);
}

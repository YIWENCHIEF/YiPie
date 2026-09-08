#pragma once
// Task 7: 系统托盘（Shell_NotifyIconW, NOTIFYICON_VERSION_4）。
// 隐藏消息窗口接收 WM_APP+1 回调；右键菜单：启用/设置/重载/退出。
// 与 main 相同线程；回调（重载/设置/退出）在窗口过程栈上执行。
#include <windows.h>
#include <functional>
#include <string>

namespace Tray {

struct Callbacks {
    std::function<void()> onReload;       // 「重新加载配置」：由宿主原地重载并自行弹气泡
    std::function<void()> onQuit;         // 「退出」：宿主 PostQuitMessage
    std::function<void()> onOpenSettings; // 「设置…」/双击图标：M2。为空则回退旧占位气泡。
};

// 创建托盘图标 + 消息窗口。失败返回 false（不抛异常）。
bool Create(HINSTANCE hi, Callbacks cb);
void Destroy();

// 气泡提示（info 或 error 图标）。utf16 文本直接入 Shell_NotifyIcon。
void ShowBalloon(const std::wstring& title, const std::wstring& text, bool error = false);

}  // namespace Tray

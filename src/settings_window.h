#pragma once
// M2 T2: WebView2 设置窗口宿主。
// 单实例顶层窗口（已开→置顶）；Evergreen 运行时探测；消息式 IPC 经 ipcrouter 转发。
// 与 main 同线程（消息泵驱动 WebView2 回调）；关闭窗口不退出主消息循环。
// 注意：TU 内禁止 WIN32_LEAN_AND_MEAN（wv2_handlers.h 顶部说明）。
#include <windows.h>
#include <functional>
#include <string>

#include "config.h"

struct SettingsHostDeps {
    AppConfig* cfg = nullptr;  // 地址稳定的活配置（g_cfg）；本模块只读

    // 宿主负责：原地赋值 g_cfg + SaveConfig + 按需 SyncAutoStart。
    // 返回空串 = 成功；非空 = 失败原因（回给 UI）。失败时宿主必须保证
    // g_cfg 已回滚到赋值前状态（内存与磁盘一致）。
    std::function<std::string(const AppConfig& incoming)> onConfigApplied;
};

namespace SettingsWindow {
bool IsAvailable();                               // Evergreen 探测（结果进程内缓存）
bool Open(HINSTANCE hi, const SettingsHostDeps& deps);  // 已开则置顶；false=失败（含运行时缺失）
void Close();                                     // 幂等
bool IsOpen();
}  // namespace SettingsWindow

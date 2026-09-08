#pragma once
// Task 5: WH_MOUSE_LL 低级鼠标钩子。
// 事件拦截、触发键映射、重放守卫、全屏/修饰键/黑名单场景放行。
// 注意：钩子与 handler 全部运行在安装线程（宿主 main）的消息循环上，
// 单线程模型，无需加锁；proc 内禁止 IO/COM/Sleep。
#include <cstdint>
#include <functional>
#include <string>

#include "config.h"

namespace MouseHook {

// 回调: 引擎决策完成后由本模块反向通知宿主。
//   onButtonDownStart: 触发键按下且所有放行短路条件通过时调用;
//     返回 true = 手势开始并拦截按下事件(return 1), false = 放行。
//   onMouseMove: 手势期间(非 Idle)的每个移动事件, 移动本身永远放行。
//   onButtonUp: 触发键抬起; 返回 true = 手势已消费, 拦截抬起事件,
//     false = 放行(是否 ReplayTriggerClick 由宿主决定, 见 Task 7)。
struct Handler {
    std::function<bool(double x, double y)> onButtonDownStart;
    std::function<void(double x, double y)> onMouseMove;
    std::function<bool(double x, double y)> onButtonUp;
};

// 安装钩子。必须带消息循环的线程调用; cfg 以引用保存(不拷贝),
// 宿主须保证 AppConfig 生命周期覆盖整个 Install..Uninstall 区间。
// 失败返回 false(不抛异常)。
bool Install(const AppConfig& cfg, Handler handler);
void Uninstall();

// 托盘"暂停"开关: 暂停时全部事件放行
void SetPaused(bool paused);
bool IsPaused();

// 用 SendInput 重放触发键的按下+抬起(magic dwExtraInfo=0x5950 标记,
// proc 首行识别并直接放行, 不会再次进入手势逻辑)。
void ReplayTriggerClick(TriggerButton btn);

// 前台进程 exe 基名(含 .exe, 小写); 失败返回空串
std::string ActiveProcessName();

// 前台窗口是否"独占/无边框铺满显示器"全屏
bool IsActiveWindowFullScreen();

}  // namespace MouseHook

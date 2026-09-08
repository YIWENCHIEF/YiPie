#pragma once
#include <windows.h>
#include <string>
#include "config.h"

// Radial wheel overlay window (Task 6).
//
// Topmost, non-activating (WS_EX_NOACTIVATE), layered window composited with
// UpdateLayeredWindow from a Direct2D-rendered 32bpp top-down DIB section.
// Mouse pass-through is decided per-point in WM_NCHITTEST (NOT
// WS_EX_TRANSPARENT): while a gesture is active the wheel disc absorbs input
// (HTCLIENT) so hover/click cannot leak to the window underneath; everywhere
// else and while idle it returns HTTRANSPARENT, so input flows through and
// the global hook keeps receiving events regardless (hooks bypass hit-testing).
//
// Threading / COM: the host (Task 7) MUST call OleInitialize() (or at least
// CoInitializeEx) on the same thread that calls Create()/Show()/Update()/Hide().
// This module never initializes COM itself.
//
// Update() semantics: stores the latest Frame; the repaint happens on the next
// WM_TIMER tick (~8ms while shown). When inactive, Update() is ignored.
//
// DPI (M1.1-②): the app manifest declares PerMonitorV2, so physical mouse
// coordinates need no translation. Geometry scaling is the HOST's job: pass
// already-scaled radii in AppearanceSettings AND call SetDpiScale() with the
// anchor monitor's dpi/96 so type and stroke widths match the geometry.
namespace WheelWindow {
struct Frame {           // 宿主每次 Show/Update 传入的渲染快照
    int sector = -1;     // 高亮扇区, -1 无
    int subSector = -1;  // 子环高亮扇区（M3b），-1 无
    bool escaped = false;
    bool showSub = false;  // 子环展开（引擎迟滞判定）
    double angle = 0;    // 指针线角度（弧度，屏幕 y 向下）
    double distance = 0; // M1 渲染层未使用，保留接口
};
bool Create(HINSTANCE hi);                       // 注册类 + D2D/DWrite 工厂 + 隐藏窗口
void SetDpiScale(double scale);                  // dpi/96；字号/标签框/线宽（几何半径由宿主缩放后传入）
void Show(double cx, double cy, const Profile& profile, const AppearanceSettings& app); // 定位+首帧+定时器
void Update(const Frame& f);                     // 触发重绘(仅 Active 期间)
void Hide();                                     // KillTimer + 隐藏，空闲零开销
bool IsActive();
}

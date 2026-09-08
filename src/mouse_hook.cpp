// Task 5: WH_MOUSE_LL 低级鼠标钩子实现。
// 安全红线：proc 只做 读 MSLLHOOKSTRUCT -> 查状态 -> 调 handler -> 返回，
// 禁止 IO/COM/Sleep/弹窗；handler 由宿主保证轻量（重活在 Task 7 宿主侧异步化）。
// 钩子回调在安装线程的消息循环上被调用，与 SetPaused/Install 等同线程，
// 故常规状态无需加锁；仅重放计数用 Interlocked 以防注入线程差异（当前无）。
// M1 收尾修正：ReplayTriggerClick 改为「延迟注入」——在钩子栈内同步 SendInput
// 会与低级钩子派发死锁式停等 ~1000ms（实测 1001/1005/1015ms），详见下文。
#include "mouse_hook.h"
#include "isolation.h"

#include <algorithm>
#include <cctype>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifdef YIPIE_LATENCY_PROBE
#include <fstream>
#endif

namespace {

// "YP" 魔数：ReplayTriggerClick 注入事件的 dwExtraInfo 标记
constexpr ULONG_PTR kReplayMagic = 0x5950ull;

HHOOK g_hook = nullptr;
const AppConfig* g_cfg = nullptr;      // 引用保存：宿主保证 cfg 生命周期覆盖 Install 区间
MouseHook::Handler g_handler;          // 值持有（std::function）
bool g_paused = false;
bool g_gestureActive = false;          // 触发键已按下且被拦截，手势进行中
volatile LONG g_replayPending = 0;     // 期望中的重放事件数（仅诊断用，放行只看魔数）

// —— 延迟重放状态（修复 ~1000ms 原生右键延迟，规格 §1 <10ms 硬指标）——
// ReplayTriggerClick 的调用点在 onButtonUp —— 即运行在我们自己的
// LowLevelMouseProc 栈上。若在此栈内同步 SendInput：注入的 down+up 必须走完整条
// WH_MOUSE_LL 钩子链（含本 proc），而本线程仍卡在本次钩子调用中、无法接收该
// 派发 —— OS 停等约 1000ms 后才让 SendInput 返回。改为 PostMessage 到
// 消息-only 窗口：钩子 proc 立即返回，紧随其后的消息泵在 wndproc（非钩子栈）
// 里执行真正的 SendInput。窗口与钩子同线程，无跨线程问题。
constexpr UINT kReplayMsg = WM_APP + 0x50;  // 私有消息 id（tray 用 WM_APP+1，无冲突）
constexpr wchar_t kReplayWndClass[] = L"YiPieReplayMessageWindow";
HWND g_replayWnd = nullptr;                 // 消息-only 窗口，属钩子安装线程
bool g_replayClassRegistered = false;
// 已调度、待 wndproc 执行的按钮（-1=无）。仅本线程读写；atomic 属防御性，
// 不构成跨线程协议（Install/钩子 proc/wndproc 同线程）。
std::atomic<LONG> g_replayPendingBtn{ -1 };

std::string ToLowerTrim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
    std::string r = s.substr(b, e - b);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

enum class TrigMsg { None, Down, Up };

// 客户区触发键消息映射。NC 消息（WM_NCRBUTTONDOWN 等）与 X 键的
// mouseData 子键不匹配都归为 None —— 一律放行，不拦截。
TrigMsg Classify(UINT msg, const MSLLHOOKSTRUCT* ms, TriggerButton trig) {
    if (trig == TriggerButton::Right) {
        if (msg == WM_RBUTTONDOWN) return TrigMsg::Down;
        if (msg == WM_RBUTTONUP) return TrigMsg::Up;
        return TrigMsg::None;
    }
    if (trig == TriggerButton::Middle) {
        if (msg == WM_MBUTTONDOWN) return TrigMsg::Down;
        if (msg == WM_MBUTTONUP) return TrigMsg::Up;
        return TrigMsg::None;
    }
    // X1/X2：必须核对 HIWORD(mouseData)，否则 X1 会误吞 X2 事件
    if (msg != WM_XBUTTONDOWN && msg != WM_XBUTTONUP) return TrigMsg::None;
    const WORD xid = HIWORD(ms->mouseData);
    const WORD want = (trig == TriggerButton::X2) ? XBUTTON2 : XBUTTON1;
    if (xid != want) return TrigMsg::None;
    return (msg == WM_XBUTTONDOWN) ? TrigMsg::Down : TrigMsg::Up;
}

bool ModifierHeld() {
    return GetAsyncKeyState(VK_CONTROL) < 0 || GetAsyncKeyState(VK_SHIFT) < 0 ||
           GetAsyncKeyState(VK_MENU) < 0 || GetAsyncKeyState(VK_LWIN) < 0 ||
           GetAsyncKeyState(VK_RWIN) < 0;
}

bool ProcessIsolated() {
    if (!g_cfg) return false;
    const std::string name = MouseHook::ActiveProcessName();  // 已是小写基名
    if (name.empty()) return false;
    const auto& gs = g_cfg->gesture;
    // M3d 白名单模式：whitelist 下不在列即隔离；blacklist 下在列才隔离。
    // 名单条目在 NormalizeConfig 已小写去空白，这里仍防御性 ToLowerTrim。
    std::vector<std::string> list;
    const auto& raw = gs.isolationMode == "whitelist" ? gs.whitelistProcesses
                                                      : gs.blacklistProcesses;
    list.reserve(raw.size());
    for (const std::string& e : raw) list.push_back(ToLowerTrim(e));
    return Isolated(gs.isolationMode, list, name);
}

// —— 规格 §5 探针（仅 YIPIE_LATENCY_PROBE=ON 编译）——
// g_replayT0 在 ReplayTriggerClick 入口打桩（= 钩子 UP 路径决定重放、调度之前）；
// 实际 SendInput 于延迟 wndproc 执行时落一行 kind=replay_e2e。
// 测量的是「用户感知的右键穿透延迟」（含消息队列一跳）—— 正是规格 §1 <10ms 本体。
#ifdef YIPIE_LATENCY_PROBE
LARGE_INTEGER g_replayT0{};
LARGE_INTEGER g_replayFreq{};
void ProbeLogReplayE2E() {
    if (g_replayT0.QuadPart == 0 || g_replayFreq.QuadPart == 0) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double ms = static_cast<double>(now.QuadPart - g_replayT0.QuadPart) * 1000.0 /
                      static_cast<double>(g_replayFreq.QuadPart);
    wchar_t tmp[MAX_PATH] = {};
    const DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH) return;
    const std::wstring path = std::wstring(tmp) + L"yipie_latency.log";
    std::ofstream f(std::wstring(path), std::ios::app | std::ios::binary);  // UTF-8/ASCII
    if (!f) return;
    f << "replay_e2e," << ms << '\n';
    f.flush();
}
#endif

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_cfg != nullptr) {
        // 规格 §3.7「钩子回调内 try/catch 全包」：宿主 handler 在本栈上执行，
        // 可能抛出的路径包括 std::thread 构造（线程耗尽 system_error）、
        // Profile/vector 拷贝的 bad_alloc、WheelWindow::Show 内部分配等。
        // 异常一旦逃逸钩子过程即 UB（大概率进程崩溃 + 全局鼠标受损）。
        // 兜底策略：fail open —— 捕获一切，把本次事件原样放行给下一个钩子。
        // 注：ReplayTriggerClick 现已非阻塞（PostMessage 即返回），SendInput 及其
        // InterlockedExchange(g_replayPending,2) 移入延迟 wndproc；本栈上任何
        // throw 点都不会留下"半个注入对"，g_replayPending 一致性不受影响。
        try {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

        // ① 重放守卫：魔数标记的注入事件绝不进入手势逻辑，直接放行给目标应用
        if (ms->dwExtraInfo == kReplayMagic) {
            if (g_replayPending > 0) InterlockedDecrement(&g_replayPending);
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        const UINT msg = static_cast<UINT>(wParam);

        // ② MOVE：手势进行中要通知宿主，但移动事件本身永远放行
        //（吞移动会毁掉宿主拖拽/悬停）
        if (msg == WM_MOUSEMOVE) {
            if (g_gestureActive && g_handler.onMouseMove) {
                g_handler.onMouseMove(static_cast<double>(ms->pt.x),
                                      static_cast<double>(ms->pt.y));
            }
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        const TrigMsg kind = Classify(msg, ms, g_cfg->gesture.trigger);
        if (kind == TrigMsg::None) {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        const double x = static_cast<double>(ms->pt.x);
        const double y = static_cast<double>(ms->pt.y);

        if (kind == TrigMsg::Down) {
            if (g_gestureActive) {
                // 同一触发键重复按下（异常/连点兜底）：放行，不重复起手
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            }
            // 短路顺序：paused -> 全屏 -> 修饰键 -> 黑名单 -> 宿主决策
            if (g_paused) return CallNextHookEx(g_hook, nCode, wParam, lParam);
            if (g_cfg->gesture.disableOnFullScreen && MouseHook::IsActiveWindowFullScreen())
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            if (g_cfg->gesture.disableOnModifier && ModifierHeld())
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            if (ProcessIsolated())
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            if (g_handler.onButtonDownStart && g_handler.onButtonDownStart(x, y)) {
                g_gestureActive = true;
                return 1;  // 拦截按下
            }
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        // TrigMsg::Up
        if (!g_gestureActive) {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
        // 手势进行中：抬起事件必须先问宿主，短路条件（全屏/黑名单等）不在此处复查，
        // 否则会把手势起手后场景切换的松手事件漏给应用造成误触。
        bool consumed = false;
        if (g_handler.onButtonUp) consumed = g_handler.onButtonUp(x, y);
        g_gestureActive = false;
        if (consumed) return 1;
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
        } catch (...) {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// 真正执行注入的内部函数：只允许从延迟 wndproc（或 PostMessage 失败的兜底路径）
// 调用 —— 即调用线程此刻【不在】钩子 proc 栈上，否则重蹈 ~1000ms 停等。
void DoReplayTriggerClick(TriggerButton btn) {
    INPUT inputs[2] = {};
    DWORD downFlag = 0, upFlag = 0;
    WORD mouseData = 0;
    switch (btn) {
        case TriggerButton::Middle:
            downFlag = MOUSEEVENTF_MIDDLEDOWN; upFlag = MOUSEEVENTF_MIDDLEUP; break;
        case TriggerButton::X1:
            downFlag = MOUSEEVENTF_XDOWN; upFlag = MOUSEEVENTF_XUP;
            mouseData = XBUTTON1; break;
        case TriggerButton::X2:
            downFlag = MOUSEEVENTF_XDOWN; upFlag = MOUSEEVENTF_XUP;
            mouseData = XBUTTON2; break;
        case TriggerButton::Right:
        default:
            downFlag = MOUSEEVENTF_RIGHTDOWN; upFlag = MOUSEEVENTF_RIGHTUP; break;
    }
    // X 键必须走 SendInput：mouse_event 无 mouseData 参数，无法携带 XBUTTON1/2
    for (int i = 0; i < 2; ++i) {
        inputs[i].type = INPUT_MOUSE;
        inputs[i].mi.dx = 0;
        inputs[i].mi.dy = 0;
        inputs[i].mi.mouseData = mouseData;
        inputs[i].mi.dwFlags = (i == 0) ? downFlag : upFlag;
        inputs[i].mi.time = 0;
        inputs[i].mi.dwExtraInfo = kReplayMagic;
    }
    InterlockedExchange(&g_replayPending, 2);
#ifdef YIPIE_LATENCY_PROBE
    ProbeLogReplayE2E();  // 实际 SendInput 发生瞬间：decision→注入 的端到端延迟
#endif
    if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
        InterlockedExchange(&g_replayPending, 0);  // 未注入则清空期望值
    }
}

// 延迟重放窗口过程：运行于钩子调用【返回之后】的消息泵迭代，非钩子栈。
LRESULT CALLBACK ReplayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kReplayMsg) {
        // 排序说明（如实注记，不过度设计）：PostMessage 进本线程队列。本次 UP 的
        // 钩子调用返回后，GetMessage 先取到这条 WM_APP+0x50 再服务后续钩子派发，
        // 正常序列下重放必定先于下一次真实按下执行。最坏情况（快速连点时系统在
        // 本消息之前派发下一笔真实 DOWN）仅是 down/up 对与新点击交错 —— 事件流
        // 仍成对平衡（每次调度恰注入一对其余不动），可接受。
        try {
            g_replayPendingBtn.store(-1, std::memory_order_relaxed);
            DoReplayTriggerClick(static_cast<TriggerButton>(static_cast<INT_PTR>(wParam)));
        } catch (...) {
            // wndproc 同样禁止异常逃逸（与钩子 proc 同一红线）。吞掉的代价仅为本
            // 次重放丢失；down/up 在单次 SendInput 内原子成对，不会造成半按状态。
            InterlockedExchange(&g_replayPending, 0);
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

}  // namespace

namespace MouseHook {

bool Install(const AppConfig& cfg, Handler handler) {
    if (g_hook != nullptr) Uninstall();  // 允许重装（Task 7 热重载路径）
    g_cfg = &cfg;
    g_handler = std::move(handler);
    g_gestureActive = false;
    g_replayPending = 0;

    // 消息-only 重放窗口：属本（安装）线程，钩子调用返回后由同一消息泵派发。
    if (g_replayWnd == nullptr) {
        if (!g_replayClassRegistered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &ReplayWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kReplayWndClass;
            if (RegisterClassExW(&wc) != 0) g_replayClassRegistered = true;
        }
        if (g_replayClassRegistered) {
            g_replayWnd = CreateWindowExW(0, kReplayWndClass, L"YiPieReplay", 0, 0, 0, 0, 0,
                                          HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                                          nullptr);
        }
        // 创建失败不致命：ReplayTriggerClick 走兜底同步路径（慢但功能不丢）。
    }

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, &LowLevelMouseProc,
                               GetModuleHandleW(nullptr), 0);
    if (g_hook == nullptr) {
        if (g_replayWnd != nullptr) {  // 回滚窗口，保持幂等
            DestroyWindow(g_replayWnd);
            g_replayWnd = nullptr;
        }
        g_replayPendingBtn.store(-1, std::memory_order_relaxed);
        g_cfg = nullptr;
        g_handler = Handler{};
        return false;
    }
    return true;
}

void Uninstall() {
    if (g_hook != nullptr) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    if (g_replayWnd != nullptr) {
        // 丢弃尚未泵到的重放消息：退出瞬间补注入会给光标下的宿主凭空弹菜单，
        // 静默放弃更符合「进程正在拆除」语义（消息此时最多 1 条，窗口刚 post 完
        // 通常已在同一 UP 的下一泵迭代执行掉了）。
        MSG m{};
        while (PeekMessageW(&m, g_replayWnd, kReplayMsg, kReplayMsg, PM_REMOVE)) {}
        DestroyWindow(g_replayWnd);
        g_replayWnd = nullptr;
    }
    g_gestureActive = false;
    g_replayPending = 0;
    g_replayPendingBtn.store(-1, std::memory_order_relaxed);
    g_cfg = nullptr;
    g_handler = Handler{};
}

void SetPaused(bool paused) { g_paused = paused; }
bool IsPaused() { return g_paused; }

// 公开入口：只调度，不注入。非阻塞 —— PostMessage 入队即返回。
// 实际 SendInput 发生在其后的 ReplayWndProc（见文件头注释与 kReplayMsg 处说明）。
void ReplayTriggerClick(TriggerButton btn) {
#ifdef YIPIE_LATENCY_PROBE
    // t0 = UP 路径决定重放、调度之前
    if (g_replayFreq.QuadPart == 0) QueryPerformanceFrequency(&g_replayFreq);
    QueryPerformanceCounter(&g_replayT0);
#endif
    g_replayPendingBtn.store(static_cast<LONG>(btn), std::memory_order_relaxed);
    if (g_replayWnd != nullptr &&
        PostMessageW(g_replayWnd, kReplayMsg, static_cast<WPARAM>(btn), 0)) {
        return;
    }
    // 兜底：消息窗口不可用（创建失败或正在拆除）。若此刻在钩子栈上会停等 ~1s，
    // 但丢点击更不可接受；且该路径仅在窗口创建失败/退出瞬间可达。
    g_replayPendingBtn.store(-1, std::memory_order_relaxed);
    DoReplayTriggerClick(btn);
}

std::string ActiveProcessName() {
    HWND hw = GetForegroundWindow();
    if (!hw) return "";
    DWORD pid = 0;
    GetWindowThreadProcessId(hw, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return "";
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    std::string r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
        std::wstring full(buf, n);
        auto base = full.substr(full.find_last_of(L'\\') + 1);
        for (auto ch : base) r += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    CloseHandle(p);
    return r;
}

bool IsActiveWindowFullScreen() {
    HWND hw = GetForegroundWindow();
    if (!hw) return false;
    // 桌面 shell 窗口（Progman 与壁纸 WorkerW）无边框且铺满显示器，会被
    // 下面的"无边框+铺满"启发式误判为独占全屏 —— 桌面恰恰是最需要轮盘的
    // 场景，按类名排除。
    wchar_t cls[64] = {};
    if (GetClassNameW(hw, cls, 63) > 0) {
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0)
            return false;
    }
    RECT r;
    if (!GetWindowRect(hw, &r)) return false;
    HMONITOR hm = MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(hm, &mi)) return false;
    const bool covered = r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
                         r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
    if (!covered) return false;
    // 排除 maximized 普通窗口(贴边但非独占): 无边框样式才视为独占全屏
    const LONG_PTR style = GetWindowLongPtrW(hw, GWL_STYLE);
    const bool borderless = !(style & (WS_CAPTION | WS_THICKFRAME));
    return borderless;
}

}  // namespace MouseHook

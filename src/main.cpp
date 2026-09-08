// Task 7: YiPie 主程序 —— 单线程接线。
// 模型：wWinMain 上跑标准 GetMessage 循环；WH_MOUSE_LL 钩子回调、手势状态机、
// 轮盘渲染（Show/Update 在钩子栈上发起、WM_TIMER 重绘）、托盘全部共用这一线程。
// 因此钩子与 UI 状态之间无锁；Fire 路径是唯一例外（executor 含 Sleep，
// 用 detached 线程执行，不碰 UI）。
//
// 地址稳定契约：MouseHook::Install 以引用保存 cfg（不拷贝）。
// 托盘「重新加载配置」必须原地赋值（g_cfg = loaded），禁止换对象重建，
// 否则钩子里的悬垂引用会 UB。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shellscalingapi.h>   // GetDpiForMonitor (M1.1-②)
#include <string>
#include <thread>
#include <optional>
#include "config.h"
#include "gesture_engine.h"
#include "mouse_hook.h"
#include "wheel_window.h"
#include "action_executor.h"
#include "icon_cache.h"
#include "tray.h"
#include "settings_window.h"

#ifdef YIPIE_LATENCY_PROBE
#include <fstream>
#endif

namespace {

// —— 规格 §5 硬指标量测仪表（CMake 选项 YIPIE_LATENCY_PROBE，默认 OFF 时零开销）——
// QPC 计时，样本按「kind,ms」一行一条追加到 %TEMP%\yipie_latency.log。
// 仅用于验收实测；生产构建不编译任何一行。
#ifdef YIPIE_LATENCY_PROBE
struct QpcTimer {
    LARGE_INTEGER start{};
    LARGE_INTEGER freq{};
    QpcTimer() {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    double ms() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (freq.QuadPart == 0) return -1.0;
        return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 /
               static_cast<double>(freq.QuadPart);
    }
};
void ProbeLog(const char* kind, double ms) {
    wchar_t tmp[MAX_PATH] = {};
    const DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n > MAX_PATH) return;
    const std::wstring path = std::wstring(tmp) + L"yipie_latency.log";
    std::ofstream f(std::wstring(path), std::ios::app | std::ios::binary);  // UTF-8/ASCII
    if (!f) return;
    f << kind << ',' << ms << '\n';
    f.flush();
}
#endif

// 全局状态（单线程访问；g_cfg 地址必须稳定，见上）
AppConfig g_cfg;                          // 钩子引用的活配置
Profile g_activeProfile;                  // 当前手势所用 profile 的【拷贝】
// M1.1-②：DPI 缩放副本。配置值语义=96DPI 逻辑像素；起手瞬间按锚点显示器
// 有效 DPI 把 gesture/appearance 的像素类字段乘以 scale，引擎与渲染都消费
// 这份缩放副本（物理坐标直通，manifest 已声明 PerMonitorV2）。
GestureSettings g_scaledGesture;
AppearanceSettings g_scaledAppearance;
std::optional<GestureEngine> g_engine;    // 每次起手重建（ctor 持缩放副本引用）
double g_startX = 0, g_startY = 0;        // 起手坐标（Show 定位锚点）

// 锚点显示器 DPI/96（失败回退 1.0）。物理像素坐标 → 显示器查询用 POINT。
double AnchorDpiScale(double x, double y) {
    POINT pt{ (LONG)x, (LONG)y };
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0)
        return (double)dx / 96.0;
    return 1.0;
}

// M3d：theme=="system" -> 读注册表实际深浅色（AppsUseLightTheme：1=light，
// 0/缺失=dark）；其它值原样返回。起手时解析一次，渲染器只见 dark/light。
std::string ResolveTheme(const std::string& theme) {
    if (theme != "system") return theme;
    DWORD v = 0, size = sizeof(v);
    const LSTATUS ls = ::RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &v, &size);
    if (ls == ERROR_SUCCESS) return v ? "light" : "dark";
    return "dark";
}

// M1.1-①：开机自启 —— 把 cfg.gesture.autoStart 同步到 HKCU Run。
// 值名固定 "YiPie"，数据为带引号的 exe 全路径（防止路径含空格被 cmd 拆参）。
// 失败只记返回值（调用方决定是否提示）；注册表 API 不抛异常。
bool SyncAutoStart(bool enable) {
    const wchar_t* kRun = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    LSTATUS ls;
    if (!enable) {
        ls = ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kRun, L"YiPie");
        return ls == ERROR_SUCCESS || ls == ERROR_FILE_NOT_FOUND;  // 本就不存在=成功
    }
    wchar_t exe[MAX_PATH * 2] = {};
    const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return false;
    const std::wstring quoted = std::wstring(L"\"") + exe + L"\"";
    ls = ::RegSetKeyValueW(HKEY_CURRENT_USER, kRun, L"YiPie", REG_SZ,
                           quoted.data(),
                           static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
    return ls == ERROR_SUCCESS;
}

// 手势活跃 = 引擎已构造且离开 Idle 相位
bool GestureActive() {
    return g_engine && g_engine->phase() != Phase::Idle;
}

void DoReload() {
    // 手势进行中拒绝换配置：引擎持有 g_cfg.gesture/appearance 的引用，
    // 原地赋值虽不悬垂，但会把手势中途的参数换掉，行为不可预期。
    if (GestureActive()) {
        Tray::ShowBalloon(L"YiPie", L"手势进行中，请稍后再重载");
        return;
    }
    AppConfig loaded;
    std::string err;
    const bool ok = LoadConfig(loaded, err);
    const bool transient = err.rfind("io-transient:", 0) == 0;
    if (ok && err.empty()) {
        g_cfg = std::move(loaded);  // 原地赋值：地址不变，钩子无感
        const bool autoOk = SyncAutoStart(g_cfg.gesture.autoStart);
        Tray::ShowBalloon(L"YiPie",
                          autoOk ? L"配置已重载"
                                 : L"配置已重载，但开机自启注册表同步失败",
                          !autoOk);
    } else if (transient || ok) {
        // transient = 文件暂被占用（典型：杀软正在扫描刚写入的配置）；
        // ok+err = 内存配置可用但回写失败。两种都【不隔离、不覆盖】——
        // 历史 bug：瞬时 IO 被当"损坏"处理会销毁用户刚配好的热键。
        Tray::ShowBalloon(L"YiPie",
            transient ? L"配置文件暂时无法读取（常被安全软件占用），本次保留当前配置"
                      : L"配置已加载，但回写文件失败（不影响本次使用）",
            true);
    } else {
        // 规格 §4：真损坏（文件能读出但 JSON 解析失败）—— 备份坏文件后
        // 保留当前运行配置（g_cfg 不动；只重写磁盘上的默认文件）。
        std::string qErr;
        const std::wstring cfgPath = ConfigFilePath();
        if (!cfgPath.empty()) QuarantineConfigFileAt(cfgPath, qErr);
        std::string saveErr;
        SaveConfig(AppConfig(), saveErr);
        Tray::ShowBalloon(L"YiPie", L"配置文件无效，已备份为 config.json.bad-*，保留当前运行配置", true);
    }
}

void QuitCleanup() {
    // 决策 #6：退出时若有活跃手势，先中止并收起轮盘再拆环境
    if (GestureActive()) {
        g_engine->OnAbort();
        WheelWindow::Hide();
    }
    SettingsWindow::Close();  // M2：设置页开着也必须先拆（幂等）
    MouseHook::Uninstall();
    Tray::Destroy();
    PostQuitMessage(0);  // 消息循环退出；循环后还有兜底 teardown（幂等）
}

// M2 T2: 托盘「设置…」/双击入口
HINSTANCE g_hinst = nullptr;

void OpenSettings() {
    if (!SettingsWindow::IsAvailable()) {
        // spec §2: 运行时缺失 -> 引导下载，核心轮盘功能不受影响
        if (MessageBoxW(nullptr,
                        L"未检测到 WebView2 运行时（Win11 通常自带）。\n是否打开微软下载页？",
                        L"YiPie 设置", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            ShellExecuteW(nullptr, L"open",
                          L"https://developer.microsoft.com/microsoft-edge/webview2/",
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    SettingsHostDeps d;
    d.cfg = &g_cfg;  // 地址稳定契约：g_cfg 永不重建
    d.onConfigApplied = [](const AppConfig& incoming) -> std::string {
        if (GestureActive()) return "手势进行中，稍后再保存";  // 与 DoReload 同策略
        const AppConfig snapshot = g_cfg;  // 保存失败时回滚，内存与磁盘保持一致
        const bool autoChanged = g_cfg.gesture.autoStart != incoming.gesture.autoStart;
        g_cfg = incoming;  // 原地赋值（地址稳定契约）
        std::string err;
        if (!SaveConfig(g_cfg, err)) {
            g_cfg = snapshot;
            return "保存失败，已回滚：" + err;
        }
        if (autoChanged) SyncAutoStart(g_cfg.gesture.autoStart);
        return {};
    };
    SettingsWindow::Open(g_hinst, d);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // ① 单实例
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"YiPie_SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;  // 静默退出
    }

    // ② D2D/DWrite 需要 STA COM（与 WheelWindow 同线程）
    const HRESULT hrOle = OleInitialize(nullptr);

    // ③ 配置：缺文件 -> LoadConfig 已写默认；解析失败 -> 用内存默认继续，
    //    气泡提示延后到托盘创建之后。bool 与 err 分开判（bool=true 且 err 非空
    //    是“默认配置可用但回写文件失败”的合法态）。
    std::string loadErr;
    const bool loaded = LoadConfig(g_cfg, loadErr);
    // io-transient（文件暂被占用）：g_cfg 保持内存默认继续跑，磁盘文件
    // 原样保留，下次启动/重载自然重试 —— 绝不隔离覆盖（历史丢配置 bug）。
    const bool ioTransient = loadErr.rfind("io-transient:", 0) == 0;
    const bool cfgProblem = (!loaded && !ioTransient) || !loadErr.empty();
    std::wstring cfgProblemW(loadErr.begin(), loadErr.end());
    if (!loaded && !ioTransient) {
        // 规格 §4：解析/读取失败说明磁盘上的文件是坏的 —— 备份坏文件
        // （config.json.bad-时间戳）并回退默认配置生成新文件。
        // g_cfg 此时保持内存默认值（LoadConfig 失败不写 out），行为不变。
        std::string qErr;
        const std::wstring cfgPath = ConfigFilePath();
        if (!cfgPath.empty() && !QuarantineConfigFileAt(cfgPath, qErr)) {
            // 隔离失败（如文件已不在）不改变启动路径：默认配置已在内存，照常运行
        }
        std::string saveErr;
        SaveConfig(AppConfig(), saveErr);  // 尽力重写默认文件；失败沿用内存默认
    }
    // M1.1-①：配置读取成功才同步自启状态（解析失败回退默认时，不动用户
    // 上一份有效配置留下的注册表状态）。失败不打断启动，M2 设置页可见可重试。
    if (loaded) SyncAutoStart(g_cfg.gesture.autoStart);

    // ④ 渲染层 + 托盘
    if (!WheelWindow::Create(hInst)) {
        MessageBoxW(nullptr, L"YiPie 初始化失败：无法创建轮盘窗口（D2D）。",
                    L"YiPie", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(hrOle)) OleUninitialize();
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    // M1.1-③（收尾 minor 兑现）：托盘创建失败必须让用户知道——没有托盘
    // 就没有重载/暂停/退出口。兜底：MessageBox 后照常进入消息循环（钩子仍可
    // 用，退出可走任务管理器），不静默失败。
    g_hinst = hInst;
    if (!Tray::Create(hInst, { &DoReload, &QuitCleanup, &OpenSettings })) {
        MessageBoxW(nullptr, L"YiPie 托盘图标创建失败。配置重载/暂停/退出暂不可用，"
                              L"进程仍在运行（结束任务可退出）。",
                    L"YiPie", MB_OK | MB_ICONWARNING);
    }
    if (cfgProblem) {
        Tray::ShowBalloon(L"YiPie",
                          loaded ? L"默认配置已就绪，但回写文件失败：" + cfgProblemW
                                 : L"配置解析失败，本次使用默认配置：" + cfgProblemW,
                          true);
    } else {
        Tray::ShowBalloon(L"YiPie", L"YiPie 已启动。右键托盘图标可暂停/重载/退出。");
    }

    // ⑤ 钩子 + 手势接线（handler 全在主线程钩子栈上执行，须轻量）
    MouseHook::Handler h;
    h.onButtonDownStart = [](double x, double y) -> bool {
        // 起手瞬间锁定 profile 拷贝：中途重载配置不会让引擎/动作数组悬垂或错位
        g_activeProfile = GetProfileForProcess(g_cfg, MouseHook::ActiveProcessName());
        // M1.1-②：按锚点（按下点）显示器 DPI 构建本手势的缩放副本。
        // 按下与轮盘锚点同屏，起手时算一次即够（手势期间跨屏移动不改尺寸，
        // 只按缩放后的阈值判命中——与所见一致）。
        const double s = AnchorDpiScale(x, y);
        g_scaledGesture = g_cfg.gesture;
        g_scaledGesture.dragThreshold *= s;
        g_scaledGesture.coreRadius *= s;
        if (g_scaledGesture.outerEscapeDistance > 0.0)
            g_scaledGesture.outerEscapeDistance *= s;
        g_scaledAppearance = g_cfg.appearance;
        g_scaledAppearance.theme = ResolveTheme(g_cfg.appearance.theme);  // M3d system
        g_scaledAppearance.wheelRadius *= s;
        g_scaledAppearance.innerRadius *= s;
        g_scaledAppearance.subWheelWidth *= s;   // M3c：子环宽度/图标尺寸同样随 DPI
        g_scaledAppearance.iconSize *= s;
        WheelWindow::SetDpiScale(s);
        g_engine.emplace(g_scaledGesture, g_scaledAppearance, g_activeProfile.sectorCount,
                         &g_activeProfile);
        g_engine->OnButtonDown(x, y);
        g_startX = x;
        g_startY = y;
        return true;  // 拦截触发键按下
    };
    h.onMouseMove = [](double x, double y) {
        if (!g_engine) return;
        const bool just = g_engine->OnMouseMove(x, y);
        if (just) {
#ifdef YIPIE_LATENCY_PROBE
            const QpcTimer t;                       // 规格 §5：轮盘弹出耗时
            WheelWindow::Show(g_startX, g_startY, g_activeProfile, g_scaledAppearance);
            ProbeLog("pop", t.ms());
#else
            WheelWindow::Show(g_startX, g_startY, g_activeProfile, g_scaledAppearance);
#endif
        }
        if (WheelWindow::IsActive()) {
            WheelWindow::Update({ g_engine->sector(), g_engine->subSector(),
                                  g_engine->escaped(), g_engine->showSubRing(),
                                  g_engine->angle(), g_engine->distance() });
        }
    };
    h.onButtonUp = [](double x, double y) -> bool {
        if (!g_engine) return false;
        const auto outcome = g_engine->OnButtonUp(x, y);
        WheelWindow::Hide();
        switch (outcome) {
        case GestureEngine::Outcome::ReplayClick: {
            // 偏差 #1（对简报骨架的修正）：DOWN 已被钩子吞掉，此处放行 UP 会让
            // 宿主收到“孤儿抬起”——部分应用见 UP 即弹菜单、部分等 DOWN 而卡半按。
            // 实测验证过的做法：消费本次 UP（return true），再用带魔数标记的
            // ReplayTriggerClick 注入一对干净的 synthetic down+up，宿主必见完整点击。
            // M1 收尾修正：ReplayTriggerClick 现在只做 PostMessage 调度（非阻塞）；
            // 实际 SendInput 在钩子调用返回后的 wndproc 里发生。规格 §5 的
            // kind=replay_e2e 样本（decision→SendInput 端到端，含队列一跳）由
            // mouse_hook.cpp 探针落盘 —— 这里再计时调用时长已无意义（恒 ~0）。
            MouseHook::ReplayTriggerClick(g_cfg.gesture.trigger);
            g_engine.reset();
            return true;
        }
        case GestureEngine::Outcome::Cancel:
            g_engine.reset();
            return true;
        case GestureEngine::Outcome::Fire: {
            const int s = g_engine->sector();
            const int ss = g_engine->subSector();
            g_engine.reset();
            if (s < 0 || s >= (int)g_activeProfile.actions.size()) return true;
            // M3b 二级寻址：子槽越界回落主槽（防御 reload 竞态）
            Action a = g_activeProfile.actions[s];
            if (ss >= 0 && ss < (int)a.subActions.size()) a = a.subActions[ss];
            if (a.type.empty()) return true;       // 空槽：静默取消
            // Execute 内含 Sleep(45ms+)，绝不能卡在钩子回调（会拖慢全局鼠标）；
            // executor 线程不碰 UI。
            // 规格 §3.7：std::thread 构造在线程耗尽时抛 system_error（就在钩子栈上）。
            // 失败时不得同步 Execute（会 Sleep ~45ms 卡全局鼠标）——跳过本次动作，
            // 气泡报错提示用户。
            try {
                std::thread([a]() mutable {
                    std::string e;
                    ActionExecutor::Execute(a, &e);
                }).detach();
            } catch (...) {
                Tray::ShowBalloon(L"YiPie", L"系统资源不足，本次轮盘动作已跳过", true);
            }
            return true;
        }
        }
        return true;
    };
    if (!MouseHook::Install(g_cfg, std::move(h))) {
        MessageBoxW(nullptr, L"YiPie 初始化失败：无法安装鼠标钩子（需要重启进程）。",
                    L"YiPie", MB_OK | MB_ICONERROR);
        Tray::Destroy();
        if (SUCCEEDED(hrOle)) OleUninitialize();
        if (mutex) CloseHandle(mutex);
        return 1;
    }

    // ⑥ 消息循环（WM_QUIT 由托盘「退出」或 QuitCleanup 路径触发）
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ⑦ 收尾
    MouseHook::Uninstall();
    Tray::Destroy();
    iconcache::Clear();   // M3c: 位图缓存随 rt 生命周期结束前释放
    if (SUCCEEDED(hrOle)) OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}

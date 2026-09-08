// M2 T2: WebView2 settings host window + live IpcHost glue.
// TU rules (M2 spike): NO WIN32_LEAN_AND_MEAN here (breaks WebView2.h /
// MIDL_INTERFACE); include order windows.h -> objbase.h -> WebView2.h.
// UNICODE 必须定义（tray.cpp 同款做法），否则 IDC_ARROW/IDI_APPLICATION 的
// MAKEINTRESOURCE 走 ANSI 分支、与 *W API 不匹配。
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>

#include "WebView2.h"
#include "wv2_handlers.h"

#include "settings_window.h"
#include "ipc_router.h"
#include "icon_flows.h"
#include "mouse_hook.h"
#include "action_executor.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

// 关窗后 Environment 保留复用（二次打开免 ~300ms 冷启动）。
// Task 6 若实测「关闭后私有 WS >= 8MB」则翻为 false —— 两条路径代码都在。
constexpr bool kKeepEnvironment = true;

constexpr LPCWSTR kWndClass = L"YiPieSettingsWindow";
constexpr int kWndW = 1080;   // 固定窗口尺寸（M3b 人工反馈：禁止缩放）
constexpr int kWndH = 760;
constexpr LPCWSTR kVirtualHost = L"yipie.app";

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return {};
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring{} : p.substr(0, slash);
}

std::wstring LocalAppData() {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return {};
    return base;
}

struct State {
    HWND hwnd = nullptr;
    ICoreWebView2Environment* env = nullptr;   // kKeepEnvironment=false 时关窗释放
    ICoreWebView2Controller* ctrl = nullptr;
    ICoreWebView2* wv = nullptr;
    SettingsHostDeps deps;
    bool creating = false;                     // env/controller 异步创建中
    ipcrouter::IpcHost host;
};
State g;

// ---- result JSON 小工具 ----------------------------------------------------

std::string PathResultJson(const std::wstring& wpath) {
    // {"path":"..."} —— 反斜杠转义为 \\ 供 JSON
    std::string p = WideToUtf8(wpath);
    std::string esc;
    esc.reserve(p.size() + 8);
    for (char c : p) {
        if (c == '\\' || c == '"') { esc += '\\'; esc += c; }
        else esc += c;
    }
    return "{\"path\":\"" + esc + "\"}";
}

// ---- live IpcHost ------------------------------------------------------------

ipcrouter::HostResult ApplyViaDeps(const std::string& cfgJsonText) {
    ipcrouter::HostResult r;
    if (!g.deps.cfg || !g.deps.onConfigApplied) {
        r.error = "host not wired";
        return r;
    }
    AppConfig tmp;
    std::string perr;
    if (!ParseConfig(cfgJsonText, tmp, perr)) {  // ParseConfig 内含 Normalize
        r.error = perr;
        return r;
    }
    const std::string applyErr = g.deps.onConfigApplied(tmp);
    if (!applyErr.empty()) {
        r.error = applyErr;  // 宿主已回滚内存配置，UI 重拉即一致
        return r;
    }
    r.ok = true;
    r.resultJson = SerializeConfig(*g.deps.cfg);  // 权威回读（含钳制结果）
    return r;
}

ipcrouter::HostResult ImportConfigFlow() {
    ipcrouter::HostResult r;
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"YiPie 配置 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"导入配置";
    if (!GetOpenFileNameW(&ofn)) {
        r.ok = true;  // 用户取消不是错误
        r.resultJson = "{\"cancelled\":true}";
        return r;
    }
    AppConfig tmp;
    std::string perr;
    if (!LoadConfigFileAt(file, tmp, perr)) {  // 校验失败绝不触碰内存配置
        r.error = "invalid config file: " + perr;
        return r;
    }
    // 转成 JSON 文本复用 apply 路径（保持单一写入口 + 权威回读）
    return ApplyViaDeps(SerializeConfig(tmp));
}

ipcrouter::HostResult ExportConfigFlow() {
    ipcrouter::HostResult r;
    if (!g.deps.cfg) { r.error = "host not wired"; return r; }
    wchar_t file[MAX_PATH * 2] = {};
    wcscpy_s(file, L"yipie-config.json");
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"YiPie 配置 (*.json)\0*.json\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"导出配置";
    if (!GetSaveFileNameW(&ofn)) {
        r.ok = true;
        r.resultJson = "{\"cancelled\":true}";
        return r;
    }
    std::string serr;
    if (!SaveConfigFileAt(file, *g.deps.cfg, serr)) {
        r.error = "export failed: " + serr;
        return r;
    }
    r.ok = true;
    r.resultJson = PathResultJson(file);
    return r;
}

ipcrouter::HostResult BrowseProgramFlow() {
    ipcrouter::HostResult r;
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"程序 (*.exe;*.lnk;*.url)\0*.exe;*.lnk;*.url\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"选择程序";
    if (!GetOpenFileNameW(&ofn)) {
        r.ok = true;
        r.resultJson = "{\"cancelled\":true}";
        return r;
    }
    r.ok = true;
    r.resultJson = PathResultJson(file);
    return r;
}

ipcrouter::HostResult TestActionFlow(const std::string& actionJsonText) {
    ipcrouter::HostResult r;
    // router 已校验 type ∈ {launch,hotkey} 且 target 非空。这里解析字段。
    rapidjson::Document d;
    d.Parse(actionJsonText.c_str());
    if (d.HasParseError() || !d.IsObject()) { r.error = "bad action"; return r; }
    Action a;
    auto getStr = [&](const char* k, std::string& out) {
        if (d.HasMember(k) && d[k].IsString())
            out.assign(d[k].GetString(), d[k].GetStringLength());
    };
    getStr("type", a.type);
    getStr("name", a.name);
    getStr("target", a.target);
    getStr("args", a.args);
    try {
        std::thread([a]() mutable {
            std::string e;
            ActionExecutor::Execute(a, &e);
        }).detach();
    } catch (...) {
        r.error = "system busy";
        return r;
    }
    r.ok = true;
    r.resultJson = "{\"launched\":true}";
    return r;
}

ipcrouter::HostResult ForegroundProcessInfo() {
    ipcrouter::HostResult r;
    const std::string proc = MouseHook::ActiveProcessName();
    std::string title;
    if (HWND hw = GetForegroundWindow()) {
        wchar_t buf[512] = {};
        const int n = GetWindowTextW(hw, buf, 511);
        if (n > 0) title = WideToUtf8(std::wstring(buf, n));
    }
    // JSON 转义 title（可能含引号/反斜杠/UTF-8 多字节——多字节原样合法）
    std::string esc;
    for (char c : title) {
        if (c == '\\' || c == '"') { esc += '\\'; esc += c; }
        else if ((unsigned char)c < 0x20) { /* 控制符直接丢弃 */ }
        else esc += c;
    }
    r.ok = true;
    r.resultJson = "{\"process\":\"" + proc + "\",\"title\":\"" + esc + "\"}";
    return r;
}

void WireHost() {
    g.host = ipcrouter::IpcHost{};
    g.host.getConfig = [] {
        ipcrouter::HostResult r;
        if (!g.deps.cfg) { r.error = "host not wired"; return r; }
        r.ok = true;
        r.resultJson = SerializeConfig(*g.deps.cfg);
        return r;
    };
    g.host.applyConfig = [](const std::string& text) { return ApplyViaDeps(text); };
    g.host.testAction = [](const std::string& text) { return TestActionFlow(text); };
    g.host.browseProgram = [] { return BrowseProgramFlow(); };
    g.host.foregroundProcess = [] { return ForegroundProcessInfo(); };
    g.host.exportConfig = [] { return ExportConfigFlow(); };
    g.host.importConfig = [] { return ImportConfigFlow(); };
    g.host.saveIcon = [](const std::string& a) {
        iconflows::HostResult f = iconflows::SaveIcon(a);
        ipcrouter::HostResult r; r.ok = f.ok; r.resultJson = f.resultJson; r.error = f.error;
        return r;
    };
    g.host.icons = [](const std::string& a) {
        rapidjson::Document d;
        d.Parse(a.c_str());
        iconflows::HostResult f;
        if (std::string(d["op"].GetString()) == "list") {
            f = iconflows::ListIcons();
        } else {
            const std::string name = d["name"].GetString();
            f = iconflows::DeleteIcon(name, [](const std::string& key) -> bool {
                if (!g.deps.cfg) return false;
                for (const Profile& pr : g.deps.cfg->profiles)
                    for (const Action& act : pr.actions) {
                        if (act.iconKey == key) return true;
                        for (const Action& sa : act.subActions)
                            if (sa.iconKey == key) return true;
                    }
                return false;
            });
        }
        ipcrouter::HostResult r; r.ok = f.ok; r.resultJson = f.resultJson; r.error = f.error;
        return r;
    };
    g.host.readIcon = [](const std::string& a) {
        rapidjson::Document d;
        d.Parse(a.c_str());
        iconflows::HostResult f = iconflows::ReadIcon(d["name"].GetString());
        ipcrouter::HostResult r; r.ok = f.ok; r.resultJson = f.resultJson; r.error = f.error;
        return r;
    };
    g.host.previewIcon = [](const std::string& a) {
        rapidjson::Document d;
        d.Parse(a.c_str());
        iconflows::HostResult f = iconflows::PreviewIcon(d["target"].GetString());
        ipcrouter::HostResult r; r.ok = f.ok; r.resultJson = f.resultJson; r.error = f.error;
        return r;
    };
}

// ---- WebView 装配 ------------------------------------------------------------

void Reply(const std::string& utf8Json) {
    if (!g.wv) return;
    const std::wstring w = Utf8ToWide(utf8Json);
    g.wv->PostWebMessageAsString(w.c_str());
}

// 导航围栏判定：只允许 yipie.app 虚拟主机（主帧与子帧共用）。
bool UriOutsideFence(LPCWSTR uri) {
    if (!uri) return false;
    const std::wstring u(uri);
    return u.rfind(L"https://yipie.app/", 0) != 0 && u != L"https://yipie.app";
}

void SetupWebView(ICoreWebView2* wv) {
    g.wv = wv;
    g.wv->AddRef();

    // 消息通道。JS 可 postMessage(object) 或 postMessage(jsonText)：前者
    // WebMessageAsJson 为对象，后者为字符串字面量——两种都接。
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> msgFn =
        [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
            LPWSTR json = nullptr;
            if (FAILED(a->get_WebMessageAsJson(&json)) || !json) {
                Reply("{\"id\":-1,\"ok\":false,\"error\":\"bad message\"}");
                return S_OK;
            }
            // WebMessageAsJson 是 UTF-16 JSON 文本：JS postMessage(string) 时
            // 它是被 JSON 包裹的字符串值；postMessage(object) 时直接是对象。两种都接。
            const std::string jsonUtf8 = WideToUtf8(json);
            CoTaskMemFree(json);
            rapidjson::Document outer;
            outer.Parse(jsonUtf8.c_str());
            std::string envelope;
            if (!outer.HasParseError() && outer.IsString()) {
                envelope.assign(outer.GetString(), outer.GetStringLength());
            } else if (!outer.HasParseError() && outer.IsObject()) {
                rapidjson::StringBuffer sb;
                rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
                outer.Accept(wr);
                envelope.assign(sb.GetString(), sb.GetSize());
            } else {
                Reply("{\"id\":-1,\"ok\":false,\"error\":\"bad message\"}");
                return S_OK;
            }
            Reply(ipcrouter::RouteIpc(envelope, g.host));
            return S_OK;
        };
    wv2::MsgReceived* msgH = new wv2::MsgReceived(std::move(msgFn));
    g.wv->add_WebMessageReceived(msgH, nullptr);
    msgH->Release();  // add_* 持有自己的引用；调用方初始引用必须释放

    // 导航围栏：只允许 yipie.app 虚拟主机（主帧）
    {
        wv2::NavStarting* h = new wv2::NavStarting(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(a->get_Uri(&uri)) && uri) {
                    if (UriOutsideFence(uri)) a->put_Cancel(TRUE);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            });
        g.wv->add_NavigationStarting(h, nullptr);
        h->Release();
        // 子帧（iframe）导航同样受围栏约束 —— postMessage 通道对所有帧开放，
        // 不封子帧等于给外部页面开了 IPC 后门（M2 收尾评审 #4）
        wv2::NavStarting* fh = new wv2::NavStarting(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                LPWSTR uri = nullptr;
                if (SUCCEEDED(a->get_Uri(&uri)) && uri) {
                    if (UriOutsideFence(uri)) a->put_Cancel(TRUE);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            });
        g.wv->add_FrameNavigationStarting(fh, nullptr);
        fh->Release();
    }
    {
        wv2::NewWindow* h = new wv2::NewWindow(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                a->put_Handled(TRUE);  // 禁止弹窗逃逸围栏
                return S_OK;
            });
        g.wv->add_NewWindowRequested(h, nullptr);
        h->Release();
    }

    ICoreWebView2Settings* st = nullptr;
    if (SUCCEEDED(g.wv->get_Settings(&st)) && st) {
        st->put_AreDefaultContextMenusEnabled(FALSE);
        st->put_IsStatusBarEnabled(FALSE);
        st->put_AreDevToolsEnabled(TRUE);  // 个人工具保留 F12；围栏(#4)与保存回滚(#6)是其安全前提
        // M3d：屏蔽浏览器 accelerator 快捷键（F5/Ctrl+P/Alt+方向 前进后退等）。
        // 设置页是应用 UI，这些快捷键只会干扰热键录制（Alt+方向被浏览器吞掉
        // 导致录不到）；文本编辑类（Ctrl+C/V）与 DevTools 不受此开关影响。
        ICoreWebView2Settings3* st3 = nullptr;
        if (SUCCEEDED(st->QueryInterface(IID_PPV_ARGS(&st3))) && st3) {
            st3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            st3->Release();
        }
        st->Release();
    }

    const std::wstring uiDir = ExeDir() + L"\\ui";
    // 本地受信页面读同目录资源 —— 虚拟主机映射用 ALLOW（导航仍被上面围栏锁死）
    ICoreWebView2_3* wv3 = nullptr;
    if (SUCCEEDED(g.wv->QueryInterface(IID_PPV_ARGS(&wv3))) && wv3) {
        wv3->SetVirtualHostNameToFolderMapping(
            kVirtualHost, uiDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        wv3->Release();
    }
    g.wv->Navigate(L"https://yipie.app/index.html");
}

void OnControllerReady(HRESULT status, ICoreWebView2Controller* ctrl) {
    g.creating = false;
    if (FAILED(status) || !ctrl) {
        if (g.hwnd) DestroyWindow(g.hwnd);
        return;
    }
    if (!g.hwnd || !IsWindow(g.hwnd)) {
        // 窗口已在异步创建期间被关：不持有 ctrl（回调持有的引用自行释放）
        return;
    }
    g.ctrl = ctrl;
    g.ctrl->AddRef();
    RECT rc{};
    GetClientRect(g.hwnd, &rc);
    g.ctrl->put_Bounds(rc);
    ICoreWebView2* wv = nullptr;
    if (SUCCEEDED(g.ctrl->get_CoreWebView2(&wv)) && wv) {
        SetupWebView(wv);
        wv->Release();
    }
    SetForegroundWindow(g.hwnd);
}

void OnEnvReady(HRESULT status, ICoreWebView2Environment* env) {
    if (FAILED(status) || !env) {
        g.creating = false;
        return;
    }
    g.env = env;
    g.env->AddRef();
    if (!g.hwnd || !IsWindow(g.hwnd)) {
        g.creating = false;
        return;  // env 已缓存（kKeepEnvironment），下次 Open 直接复用
    }
    env->CreateCoreWebView2Controller(
        g.hwnd, new wv2::CtrlCreated([](HRESULT s, ICoreWebView2Controller* c) { OnControllerReady(s, c); return S_OK; }));
}

// ---- 窗口过程 ---------------------------------------------------------------

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        // 双保险：min=max 锁死 1080x760；连"最大化"的目标尺寸也锁成同值
        // （ptMaxSize 控制最大化后的尺寸，不锁的话双击标题栏仍能撑满屏幕）。
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = kWndW;
        mmi->ptMinTrackSize.y = kWndH;
        mmi->ptMaxTrackSize.x = kWndW;
        mmi->ptMaxTrackSize.y = kWndH;
        mmi->ptMaxSize.x = kWndW;
        mmi->ptMaxSize.y = kWndH;
        mmi->ptMaxPosition.x = 0;
        mmi->ptMaxPosition.y = 0;
        return 0;
    }
    case WM_SYSCOMMAND:
        // 封死最大化入口（任务栏/快捷键/系统菜单）；其余系统命令
        // （SC_MOVE 拖拽、SC_MINIMIZE、SC_CLOSE...）必须原样交给 DefWindowProc，
        // 注意：这里绝不能 break——switch 之后函数没有返回值，会 UB。
        if ((wp & 0xFFF0) == 0xF030 /*SC_MAXIMIZE*/) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_NCLBUTTONDBLCLK:
        // 双击标题栏默认触发最大化——吞掉 caption 双击；其余区域照常处理
        if (wp == HTCAPTION) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_SIZE:
        if (g.ctrl) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            g.ctrl->put_Bounds(rc);
        }
        return 0;
    case WM_CLOSE:
        // 只拆 webview 层，窗口随后销毁；绝不 PostQuitMessage（主循环属于宿主）
        if (g.ctrl) { g.ctrl->Release(); g.ctrl = nullptr; }
        if (g.wv) { g.wv->Release(); g.wv = nullptr; }
        if (!kKeepEnvironment && g.env) { g.env->Release(); g.env = nullptr; }
        g.creating = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g.hwnd == hwnd) g.hwnd = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

BOOL RegisterWndClass(HINSTANCE hi) {
    static bool registered = false;
    if (registered) return TRUE;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWndClass;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

}  // namespace

namespace SettingsWindow {

bool IsAvailable() {
    static int cached = -1;
    if (cached == -1) {
        LPWSTR version = nullptr;
        const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
        cached = (SUCCEEDED(hr) && version && *version) ? 1 : 0;
        if (version) CoTaskMemFree(version);
    }
    return cached == 1;
}

bool Open(HINSTANCE hi, const SettingsHostDeps& deps) {
    if (IsOpen()) {
        ShowWindow(g.hwnd, SW_RESTORE);
        SetForegroundWindow(g.hwnd);
        g.deps = deps;  // 刷新回调闭包（main 重启接线场景）
        return true;
    }
    if (!IsAvailable()) return false;
    if (!RegisterWndClass(hi)) return false;

    g.deps = deps;
    WireHost();

    // M3b 人工反馈：设置窗口固定尺寸、不可缩放（无 WS_THICKFRAME/WS_MAXIMIZEBOX），
    // 一屏容纳全部功能。1080x760 = 轮盘页(预览+侧栏)与三页卡片布局的完整高度。
    g.hwnd = CreateWindowExW(0, kWndClass, L"YiPie 设置",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, kWndW, kWndH,
                             nullptr, nullptr, hi, nullptr);
    if (!g.hwnd) return false;
    ShowWindow(g.hwnd, SW_SHOW);

    if (g.env) {  // 复用保留的 Environment：直接建 controller
        g.creating = true;
        g.env->CreateCoreWebView2Controller(
            g.hwnd, new wv2::CtrlCreated([](HRESULT s, ICoreWebView2Controller* c) { OnControllerReady(s, c); return S_OK; }));
        return true;
    }

    std::wstring userData = LocalAppData();
    if (!userData.empty()) {
        userData += L"\\YiPie\\EBWebView";
        SHCreateDirectoryExW(nullptr, userData.c_str(), nullptr);
    } else {
        userData.clear();  // 探测失败交给 WebView2 选默认位置
    }

    g.creating = true;
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.empty() ? nullptr : userData.c_str(), nullptr,
        new wv2::EnvCreated([](HRESULT s, ICoreWebView2Environment* e) { OnEnvReady(s, e); return S_OK; }));
    if (FAILED(hr)) {
        g.creating = false;
        DestroyWindow(g.hwnd);
        g.hwnd = nullptr;
        return false;
    }
    return true;
}

void Close() {
    // 走 WM_CLOSE 以释放 COM 资源（DestroyWindow 不经过 WM_CLOSE，直接调会泄漏）
    if (g.hwnd && IsWindow(g.hwnd)) SendMessageW(g.hwnd, WM_CLOSE, 0, 0);
}

bool IsOpen() { return g.hwnd != nullptr && IsWindow(g.hwnd); }

}  // namespace SettingsWindow

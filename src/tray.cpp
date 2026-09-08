// Task 7: 系统托盘实现。
// 隐藏消息窗口 + Shell_NotifyIconW（NOTIFYICON_VERSION_4）。
// 菜单：启用 YiPie（checkable, 直连 MouseHook::SetPaused）/
//       重新加载配置（回调宿主，原地赋值保地址稳定）/ 退出（PostQuitMessage）。
// 双击图标 -> 气泡提示（M1 无设置页）。品牌图标由 src/yipie.rc 的 IDI_YIPIE 提供
// （M1.1-③ 落地），加载失败回退 IDI_APPLICATION。
// 本项目未在全局定义 UNICODE/_WIN32_IE（其它 TU 只用显式 W API）；
// 托盘需要 IDI_APPLICATION 的 MAKEINTRESOURCEW 宽映射与 NOTIFYICON_VERSION_4
// （_WIN32_IE>=0x0600/0x0A00 门控），故在本 TU 顶部先行定义。
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif
#include "tray.h"

#include "mouse_hook.h"

#include <shellapi.h>
#include <shlobj.h>
#include <string>

namespace {

constexpr UINT kTrayMsg = WM_APP + 1;      // 回调消息
constexpr UINT kTrayId = 1;               // 图标 id
constexpr WORD kEnableCmd = 1001;
constexpr WORD kSettingsCmd = 1004;
constexpr WORD kReloadCmd = 1002;
constexpr WORD kQuitCmd = 1003;

HWND g_hwnd = nullptr;
Tray::Callbacks g_cb;

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kTrayMsg) {
        const UINT ev = LOWORD(lp);  // v4：低 16 位是鼠标/通知消息
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kEnableCmd, L"启用 YiPie");
            CheckMenuItem(menu, kEnableCmd,
                          MouseHook::IsPaused() ? MF_UNCHECKED : MF_CHECKED);
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kSettingsCmd, L"设置…");
            AppendMenuW(menu, MF_STRING, kReloadCmd, L"重新加载配置");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kQuitCmd, L"退出");

            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);  // TrackPopupMenu 文档要求：保证点菜单外能收起
            const WORD cmd = static_cast<WORD>(TrackPopupMenuEx(
                menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD,
                pt.x, pt.y, hwnd, nullptr));
            PostMessageW(hwnd, WM_NULL, 0, 0);  // 强制立即收起
            DestroyMenu(menu);

            switch (cmd) {
            case kEnableCmd:
                MouseHook::SetPaused(!MouseHook::IsPaused());
                break;
            case kSettingsCmd:
                if (g_cb.onOpenSettings) g_cb.onOpenSettings();
                break;
            case kReloadCmd:
                if (g_cb.onReload) g_cb.onReload();
                break;
            case kQuitCmd:
                // 交给宿主做手势中止/收尾，然后退出消息循环
                if (g_cb.onQuit) g_cb.onQuit();
                break;
            default:
                break;
            }
            return 0;
        }
        if (ev == WM_LBUTTONDBLCLK) {
            if (g_cb.onOpenSettings) {  // M2: 双击直达设置页
                g_cb.onOpenSettings();
                return 0;
            }
            // 兜底（未接设置页时保留 M1 占位提示）
            wchar_t appdata[MAX_PATH] = {};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
                const std::wstring path = std::wstring(appdata) + L"\\YiPie\\config.json";
                Tray::ShowBalloon(L"YiPie",
                                  L"M2 将提供设置界面，当前请编辑 " + path);
            } else {
                Tray::ShowBalloon(L"YiPie", L"M2 将提供设置界面");
            }
            return 0;
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        // 托盘图标生命周期跟随窗口：显式删除，避免幽灵图标
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        g_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

namespace Tray {

bool Create(HINSTANCE hi, Callbacks cb) {
    g_cb = std::move(cb);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayWndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"YiPieTrayWindow";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    g_hwnd = CreateWindowExW(0, L"YiPieTrayWindow", L"YiPie", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    // M1.1-③：品牌图标（IDI_YIPIE=101，见 src/yipie.rc）。加载失败回退系统图标。
    nid.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(101));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"YiPie");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;

    // 本 SDK 的 NOTIFYICONDATAW 使用 uVersion 联合体成员（NIM_SETVERSION 不用 uFlags）
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void Destroy() {
    if (g_hwnd) DestroyWindow(g_hwnd);  // WM_DESTROY 里删图标
}

void ShowBalloon(const std::wstring& title, const std::wstring& text, bool error) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO | NIF_TIP;
    wcscpy_s(nid.szTip, L"YiPie");  // v4 下带 NIF_TIP 刷新，避免 tooltip 丢失
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    nid.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

}  // namespace Tray

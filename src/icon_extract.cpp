#include "icon_extract.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>

namespace {

// 解析 .lnk 的真实目标可执行文件路径（问题2：快捷方式取目标图标，无箭头角标）
bool ResolveShortcutTarget(const std::wstring& path, std::wstring& outTarget) {
    IShellLinkW* sl = nullptr;
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&sl)))) {
        if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf))) &&
            SUCCEEDED(pf->Load(path.c_str(), STGM_READ))) {
            wchar_t buf[MAX_PATH * 2] = {};
            if (SUCCEEDED(sl->GetPath(buf, MAX_PATH * 2, nullptr, SLGP_UNCPRIORITY)) &&
                buf[0]) { outTarget = buf; ok = true; }
        }
        if (pf) pf->Release();
        sl->Release();
    }
    return ok;
}

bool EndsWithLower(const std::wstring& p, const wchar_t* ext) {
    const size_t elen = wcslen(ext);
    if (p.size() < elen) return false;
    std::wstring tail = p.substr(p.size() - elen);
    for (auto& c : tail) c = (wchar_t)towlower(c);
    return tail == ext;
}

// 从文件（exe/dll/ico）内嵌资源提取指定尺寸图标，取最接近 sizePx 的
HICON ExtractFromExecutable(const std::wstring& file, int sizePx) {
    // 先探测该文件图标总数，逐个 PrivateExtractIcons 取 sizePx，命中即止
    const UINT total = PrivateExtractIconsW(file.c_str(), -1, sizePx, sizePx,
                                            nullptr, nullptr, 0, 0);
    if (total == 0 || total == static_cast<UINT>(-1)) {
        // 单图标文件：索引 0
        HICON icons[1] = { nullptr };
        UINT resIdx[1] = { 0 };
        if (PrivateExtractIconsW(file.c_str(), 0, sizePx, sizePx, icons, resIdx, 1, 0) >= 1 &&
            icons[0])
            return icons[0];
        return nullptr;
    }
    // 多图标：优先请求的索引 0（通常是主图标）
    HICON icons[1] = { nullptr };
    UINT resIdx[1] = { 0 };
    if (PrivateExtractIconsW(file.c_str(), 0, sizePx, sizePx, icons, resIdx, 1, 0) >= 1 &&
        icons[0])
        return icons[0];
    return nullptr;
}

}  // namespace

HICON ExtractCleanIcon(const std::wstring& path, int sizePx) {
    // 1) .lnk/.url：解析目标程序，从目标 exe 提取高分辨率图标（无 shell 箭头
    //    overlay —— overlay 只在 shell 视图绘制时叠加，直接读目标文件即干净）
    std::wstring work = path;
    if (EndsWithLower(path, L".lnk")) {
        std::wstring tgt;
        if (ResolveShortcutTarget(path, tgt)) work = tgt;
    }

    // 2) 目标/exe：PrivateExtractIcons 高分辨率
    if (HICON h = ExtractFromExecutable(work, sizePx)) return h;

    // 3) SHDefExtractIconW（exe/dll/ico 内嵌，指定尺寸）
    HICON h = nullptr;
    if (SUCCEEDED(SHDefExtractIconW(work.c_str(), 0, 0, &h, nullptr,
                                    static_cast<UINT>(sizePx))) && h)
        return h;

    // 4) 最后兜底：shell 大图标（可能低分辨率/带箭头，但至少有图）
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
        return sfi.hIcon;
    return nullptr;
}

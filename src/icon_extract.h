#pragma once
// M3d：共享图标提取 —— 解析文件（含 .lnk/.url 快捷方式）的真实图标，
// 高分辨率提取且无左下角箭头角标。icon_cache（渲染）与 icon_flows（预览）
// 都用它，保证两处一致。
#include <windows.h>
#include <string>

// 返回 sizePx 尽力高分辨率的 HICON（调用方 DestroyIcon）；失败返回 nullptr。
// 关键：用 SHGFI_ICONLOCATION 解析图标位置（对 .lnk 会解析到目标程序且无
// overlay），再 PrivateExtractIconsW 取高分辨率 —— 避免 SHGetFileInfoW 的
// 快捷方式箭头角标与低分辨率模糊。
HICON ExtractCleanIcon(const std::wstring& path, int sizePx);

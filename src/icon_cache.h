#pragma once
// M3c T2: 位图图标缓存 —— launch 提取（SHDefExtractIconW/SHGetFileInfoW）与
// 导入 PNG（WIC）统一入口；进程级 LRU（64 张 64x64 BGRA ≈ 1MB）。
// 仅 UI 线程访问（与 g.rt 同线程），无锁。
#include <string>
#include "icon_lookup.h"

struct ID2D1Bitmap;
struct ID2D1RenderTarget;

namespace iconcache {
// 返回可直接 DrawBitmap 的位图；null = 提取/解码失败（调用方回退字形）。
// file 字段语义：含路径分隔符或 .exe/.lnk/.url 结尾 = launch 目标（提取）；
// 否则视为 %APPDATA%\YiPie\icons\ 下文件名（WIC 解码）。
ID2D1Bitmap* Get(ID2D1RenderTarget* rt, const IconRef& ref);
void Clear();   // 退出时 Release 全部位图
}  // namespace iconcache

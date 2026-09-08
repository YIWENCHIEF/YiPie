#pragma once
// M3c T1: 扇区图标解析（纯逻辑，无 Win32 依赖，可单测）。
// 优先级：iconKey 手动值（g:/f: 前缀，M3a 归一化已保证合法）
//        > hotkey 语义匹配（glyphs.json patterns）
//        > launch 按 target 基名匹配
//        > app 兜底字形。
#include <string>
#include "config.h"

struct IconRef {
    bool isGlyph = false;
    wchar_t codepoint = 0;
    std::string file;   // "f:" 导入图标文件名（%APPDATA%\YiPie\icons\ 下相对名）
};

IconRef IconFor(const Action& a);
wchar_t GlyphCp(const char* id);   // 查无返回 0

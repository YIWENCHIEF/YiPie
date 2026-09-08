#pragma once
// M3c T3: 图标导入/管理流程（纯 Win32 文件操作，与 UI/COM 解耦）。
#include <functional>
#include <string>

namespace iconflows {

struct HostResult {
    bool ok = false;
    std::string resultJson;
    std::string error;
};

// name 已由 router 校验（字符集/长度/无 ..）。SaveIcon 解码 base64 PNG 落盘。
HostResult SaveIcon(const std::string& argsText);   // {name, base64Png}
HostResult ListIcons();                              // {"files":[...]}
using IsReferencedFn = std::function<bool(const std::string& iconKey)>;
HostResult DeleteIcon(const std::string& name, const IsReferencedFn& referenced);
HostResult ReadIcon(const std::string& name);        // {"base64":...}
// M3d 同步预览：提取 exe/lnk 自身图标并编码为 PNG base64（64x64）。
HostResult PreviewIcon(const std::string& target);

}  // namespace iconflows

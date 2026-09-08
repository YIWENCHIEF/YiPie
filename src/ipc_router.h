#pragma once
// M2 T1: IPC command router — pure logic (rapidjson + std only; no Win32).
// Envelope in:  {"id":<int64>,"cmd":<string>,"args":<object>}
// Envelope out: {"id":<int64>,"ok":<bool>,"result":<object>?,"error":<string>?}
// RouteIpc never throws: every failure path becomes an error reply so the
// host window can always answer the WebView (see spec §3 / plan Task 1).
#include <functional>
#include <string>

namespace ipcrouter {

struct HostResult {
    bool ok = false;
    std::string resultJson;  // JSON object text to embed as reply.result; "" => none
    std::string error;       // human-readable UTF-8 when ok == false
};

struct IpcHost {
    std::function<HostResult()> getConfig;
    // cfgJsonText: serialized args.config value (any JSON type — the host
    // side validates it as a config object; the router does not).
    std::function<HostResult(const std::string& cfgJsonText)> applyConfig;
    // actionJsonText: serialized args.action object text. The router has
    // already enforced: type in {launch,hotkey} and non-empty string target.
    std::function<HostResult(const std::string& actionJsonText)> testAction;
    std::function<HostResult()> browseProgram;        // {"path":...} or {"cancelled":true}
    std::function<HostResult()> foregroundProcess;    // {"process":...,"title":...}
    std::function<HostResult()> exportConfig;         // {"path":...} or {"cancelled":true}
    std::function<HostResult()> importConfig;         // success: result = 归一化后的完整配置对象（与 getConfig 同形）
    // M3c T3：图标导入/管理。argsText = 校验后的 args 对象 JSON 文本。
    // saveIcon args {name, base64Png}：router 已校验 name 字符集 [A-Za-z0-9_.-]
    //   （拒绝 .. 与路径分隔符）且 ≤64 字符、base64Png 非空。
    std::function<HostResult(const std::string& argsText)> saveIcon;
    // icons args {op:"list"|"delete", name?}：delete 必须带合法 name。
    std::function<HostResult(const std::string& argsText)> icons;
    // readIcon args {name}：返回 {"base64":<png base64>} 供预览缩略图。
    std::function<HostResult(const std::string& argsText)> readIcon;
    // M3d previewIcon args {target}：提取 exe 自身图标 -> {"base64":<png>}（预览同步）
    std::function<HostResult(const std::string& argsText)> previewIcon;
};

// msgJson = the raw postMessage payload text. Returns the full reply envelope.
std::string RouteIpc(const std::string& msgJson, IpcHost& host);

}  // namespace ipcrouter

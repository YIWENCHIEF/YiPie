#include "ipc_router.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::StringBuffer;
using rapidjson::Value;
using Writer = rapidjson::Writer<StringBuffer>;

namespace ipcrouter {
namespace {

// 把任意 JSON 值序列化成紧凑文本（UTF-8 原样保留），作为 host 回调入参。
std::string ValueToJsonText(const Value& v) {
    StringBuffer sb;
    Writer w(sb);
    v.Accept(w);
    return std::string(sb.GetString(), sb.GetSize());
}

// 信封构建：id + ok 必发；resultJson 非空且为合法对象时嵌入 result；
// 失败回复必含非空 error（host 回空错误串时兜底）。
std::string BuildReply(int64_t id, bool ok, const std::string& resultJson,
                       const std::string& error) {
    Document res;
    bool haveResult = false;
    std::string effErr = error;
    if (ok && !resultJson.empty()) {
        res.Parse(resultJson.c_str());
        haveResult = !res.HasParseError() && res.IsObject();
        if (!haveResult) {
            ok = false;  // host 返回了非法 result：降级为失败，绝不写畸形 JSON
            if (effErr.empty()) effErr = "host returned invalid result";
        }
    }
    if (!ok && effErr.empty()) effErr = "error";

    StringBuffer sb;
    Writer w(sb);
    w.StartObject();
    w.Key("id"); w.Int64(id);
    w.Key("ok"); w.Bool(ok);
    if (haveResult) {
        w.Key("result");
        res.Accept(w);
    }
    if (!ok) {
        w.Key("error");
        w.String(effErr.data(), static_cast<SizeType>(effErr.size()));
    }
    w.EndObject();
    return std::string(sb.GetString(), sb.GetSize());
}

}  // namespace

std::string RouteIpc(const std::string& msgJson, IpcHost& host) {
    try {
        Document doc;
        doc.Parse(msgJson.c_str());
        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("cmd") ||
            !doc["cmd"].IsString()) {
            int64_t id = -1;
            if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("id") &&
                doc["id"].IsInt64())
                id = doc["id"].GetInt64();
            return BuildReply(id, false, "", "bad envelope");
        }
        const std::string cmd(doc["cmd"].GetString(), doc["cmd"].GetStringLength());
        if (!doc.HasMember("id") || !doc["id"].IsInt64()) {
            return BuildReply(-1, false, "", "bad envelope");
        }
        const int64_t id = doc["id"].GetInt64();

        auto route = [&](auto& fnPtr, const std::string& /*cmdTag*/) {
            if (!fnPtr) return BuildReply(id, false, "", cmd + " not wired");
            try {
                HostResult r = fnPtr();
                if (!r.ok) return BuildReply(id, false, "", r.error);
                return BuildReply(id, true, r.resultJson, "");
            } catch (...) {
                return BuildReply(id, false, "", "internal");
            }
        };

        if (cmd == "getConfig") return route(host.getConfig, cmd);
        if (cmd == "browseProgram") return route(host.browseProgram, cmd);
        if (cmd == "foregroundProcess") return route(host.foregroundProcess, cmd);
        if (cmd == "exportConfig") return route(host.exportConfig, cmd);
        if (cmd == "importConfig") return route(host.importConfig, cmd);

        if (cmd == "applyConfig" || cmd == "testAction") {
            const char* argKey = (cmd == "applyConfig") ? "config" : "action";
            if (!doc.HasMember("args") || !doc["args"].IsObject() ||
                !doc["args"].HasMember(argKey)) {
                return BuildReply(id, false, "", "bad envelope");
            }
            const Value& argVal = doc["args"][argKey];
            if (cmd == "testAction") {
                // router 层校验（spec §3）：type ∈ {launch,hotkey} + 非空 target。
                if (!argVal.IsObject() || !argVal.HasMember("type") ||
                    !argVal["type"].IsString() || !argVal.HasMember("target") ||
                    !argVal["target"].IsString() ||
                    argVal["target"].GetStringLength() == 0) {
                    return BuildReply(id, false, "", "invalid action");
                }
                const std::string t(argVal["type"].GetString(),
                                    argVal["type"].GetStringLength());
                if (t != "launch" && t != "hotkey") {
                    return BuildReply(id, false, "", "invalid action");
                }
            }
            const std::string argText = ValueToJsonText(argVal);
            auto call = [&](auto& fnPtr) {
                if (!fnPtr) return BuildReply(id, false, "", cmd + " not wired");
                try {
                    HostResult r = fnPtr(argText);
                    if (!r.ok) return BuildReply(id, false, "", r.error);
                    return BuildReply(id, true, r.resultJson, "");
                } catch (...) {
                    return BuildReply(id, false, "", "internal");
                }
            };
            if (cmd == "applyConfig") return call(host.applyConfig);
            return call(host.testAction);
        }

        if (cmd == "saveIcon" || cmd == "icons" || cmd == "readIcon" || cmd == "previewIcon") {
            if (!doc.HasMember("args") || !doc["args"].IsObject())
                return BuildReply(id, false, "", "bad envelope");
            const Value& args = doc["args"];
            auto validName = [](const std::string& n) {
                if (n.empty() || n.size() > 64) return false;
                if (n.find("..") != std::string::npos) return false;
                for (char c : n) {
                    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                    (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
                    if (!ok) return false;
                }
                return true;
            };
            if (cmd == "saveIcon") {
                if (!args.HasMember("name") || !args["name"].IsString() ||
                    !args.HasMember("base64Png") || !args["base64Png"].IsString() ||
                    args["base64Png"].GetStringLength() == 0 ||
                    !validName(std::string(args["name"].GetString(),
                                           args["name"].GetStringLength())))
                    return BuildReply(id, false, "", "bad icon args");
            } else if (cmd == "readIcon") {
                if (!args.HasMember("name") || !args["name"].IsString() ||
                    !validName(std::string(args["name"].GetString(),
                                           args["name"].GetStringLength())))
                    return BuildReply(id, false, "", "bad icon args");
            } else if (cmd == "previewIcon") {
                if (!args.HasMember("target") || !args["target"].IsString() ||
                    args["target"].GetStringLength() == 0 ||
                    args["target"].GetStringLength() > 512)
                    return BuildReply(id, false, "", "bad icon args");
            } else {  // icons
                if (!args.HasMember("op") || !args["op"].IsString())
                    return BuildReply(id, false, "", "bad icon args");
                const std::string op(args["op"].GetString(), args["op"].GetStringLength());
                if (op == "delete") {
                    if (!args.HasMember("name") || !args["name"].IsString() ||
                        !validName(std::string(args["name"].GetString(),
                                               args["name"].GetStringLength())))
                        return BuildReply(id, false, "", "bad icon args");
                } else if (op != "list") {
                    return BuildReply(id, false, "", "bad icon args");
                }
            }
            const std::string argsText = ValueToJsonText(args);
            auto& fn = (cmd == "saveIcon") ? host.saveIcon
                       : (cmd == "readIcon") ? host.readIcon
                       : (cmd == "previewIcon") ? host.previewIcon
                       : host.icons;
            if (!fn) return BuildReply(id, false, "", cmd + " not wired");
            try {
                HostResult r = fn(argsText);
                if (!r.ok) return BuildReply(id, false, "", r.error);
                return BuildReply(id, true, r.resultJson, "");
            } catch (...) {
                return BuildReply(id, false, "", "internal");
            }
        }

        return BuildReply(id, false, "", "unknown command");
    } catch (...) {
        return R"({"id":-1,"ok":false,"error":"internal"})";
    }
}

}  // namespace ipcrouter

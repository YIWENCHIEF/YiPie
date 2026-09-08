#include "doctest.h"
#include "ipc_router.h"
#include "rapidjson/document.h"
#include <stdexcept>
using rapidjson::Document;

namespace {
ipcrouter::IpcHost MakeHost() {
    ipcrouter::IpcHost h;
    h.getConfig = [] { return ipcrouter::HostResult{true, R"({"appearance":{"theme":"dark"}})", ""}; };
    h.applyConfig = [](const std::string& cfg) {
        ipcrouter::HostResult r;
        if (cfg.find("\"bad\"") != std::string::npos) { r.error = "parse failed"; return r; }
        r.ok = true; r.resultJson = cfg; return r;
    };
    h.testAction = [](const std::string&) {
        ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"launched":true})"; return r;
    };
    h.browseProgram = [] { ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"path":"C:\\a.exe"})"; return r; };
    h.foregroundProcess = [] { ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"process":"notepad.exe","title":"x"})"; return r; };
    h.exportConfig = [] { ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"path":"C:\\out.json"})"; return r; };
    h.importConfig = [] { ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"appearance":{"theme":"light"}})"; return r; };
    h.saveIcon = [](const std::string&) { ipcrouter::HostResult r; r.ok = true;
        r.resultJson = R"({"iconKey":"f:x.png"})"; return r; };
    h.icons = [](const std::string&) { ipcrouter::HostResult r; r.ok = true;
        r.resultJson = R"({"files":[]})"; return r; };
    return h;
}
Document Parse(const std::string& s) { Document d; d.Parse(s.c_str()); return d; }
}

TEST_CASE("getConfig: 信封与 result 嵌入") {
    auto h = MakeHost();
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":7,"cmd":"getConfig","args":{}})", h));
    REQUIRE_FALSE(r.HasParseError());
    CHECK(r["id"].GetInt() == 7);
    CHECK(r["ok"].GetBool());
    CHECK(std::string(r["result"]["appearance"]["theme"].GetString()) == "dark");
}

TEST_CASE("applyConfig: args.config 以对象文本传 host，回读嵌入") {
    auto h = MakeHost();
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":1,"cmd":"applyConfig","args":{"config":{"x":1,"s":"中"}}})", h));
    CHECK(r["ok"].GetBool());
    CHECK(r["result"]["x"].GetInt() == 1);  // fake 回显了 cfg 文本
    CHECK(std::string(r["result"]["s"].GetString()) == "中");
}

TEST_CASE("applyConfig: host 失败 -> ok=false + error") {
    auto h = MakeHost();
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":2,"cmd":"applyConfig","args":{"config":"bad"}})", h));
    CHECK_FALSE(r["ok"].GetBool());
    CHECK(std::string(r["error"].GetString()) == "parse failed");
}

TEST_CASE("testAction: router 层校验 type 与 target") {
    auto h = MakeHost();
    // 非法：type 不在白名单
    Document r1 = Parse(ipcrouter::RouteIpc(R"({"id":3,"cmd":"testAction","args":{"action":{"type":"rm","target":"x"}}})", h));
    CHECK_FALSE(r1["ok"].GetBool());
    CHECK(std::string(r1["error"].GetString()) == "invalid action");
    // 非法：target 空
    Document r2 = Parse(ipcrouter::RouteIpc(R"({"id":4,"cmd":"testAction","args":{"action":{"type":"launch","target":""}}})", h));
    CHECK_FALSE(r2["ok"].GetBool());
    // 合法
    Document r3 = Parse(ipcrouter::RouteIpc(R"({"id":5,"cmd":"testAction","args":{"action":{"type":"hotkey","target":"ctrl+c"}}})", h));
    CHECK(r3["ok"].GetBool());
    CHECK(r3["result"]["launched"].GetBool());
}

TEST_CASE("saveIcon: name 字符集/长度校验，args 文本透传") {
    ipcrouter::IpcHost h = MakeHost();
    std::string gotArgs;
    h.saveIcon = [&](const std::string& a) { gotArgs = a;
        ipcrouter::HostResult r; r.ok = true; r.resultJson = R"({"iconKey":"f:ok.png"})"; return r; };
    // 合法
    Document ok = Parse(ipcrouter::RouteIpc(
        R"({"id":20,"cmd":"saveIcon","args":{"name":"ok.png","base64Png":"iVBORw0K"}})", h));
    CHECK(ok["ok"].GetBool());
    CHECK(ok["result"]["iconKey"].GetString() == std::string("f:ok.png"));
    CHECK(gotArgs.find("ok.png") != std::string::npos);
    // 非法 name：路径穿越
    Document bad1 = Parse(ipcrouter::RouteIpc(
        R"({"id":21,"cmd":"saveIcon","args":{"name":"..\evil","base64Png":"x"}})", h));
    CHECK_FALSE(bad1["ok"].GetBool());
    // 非法 name：含 / 或非法字符
    Document bad2 = Parse(ipcrouter::RouteIpc(
        R"({"id":22,"cmd":"saveIcon","args":{"name":"a/b.png","base64Png":"x"}})", h));
    CHECK_FALSE(bad2["ok"].GetBool());
    // 空 base64
    Document bad3 = Parse(ipcrouter::RouteIpc(
        R"({"id":23,"cmd":"saveIcon","args":{"name":"a.png","base64Png":""}})", h));
    CHECK_FALSE(bad3["ok"].GetBool());
}

TEST_CASE("icons: op 白名单，delete 需 name") {
    ipcrouter::IpcHost h = MakeHost();
    h.icons = [](const std::string& a) { ipcrouter::HostResult r; r.ok = true;
        r.resultJson = a.find("list") != std::string::npos ? R"({"files":[]})" : R"({"deleted":true})";
        return r; };
    Document list = Parse(ipcrouter::RouteIpc(
        R"({"id":24,"cmd":"icons","args":{"op":"list"}})", h));
    CHECK(list["ok"].GetBool());
    CHECK(list["result"]["files"].IsArray());
    Document del = Parse(ipcrouter::RouteIpc(
        R"({"id":25,"cmd":"icons","args":{"op":"delete","name":"x.png"}})", h));
    CHECK(del["ok"].GetBool());
    Document badOp = Parse(ipcrouter::RouteIpc(
        R"({"id":26,"cmd":"icons","args":{"op":"purge"}})", h));
    CHECK_FALSE(badOp["ok"].GetBool());
    Document noName = Parse(ipcrouter::RouteIpc(
        R"({"id":27,"cmd":"icons","args":{"op":"delete"}})", h));
    CHECK_FALSE(noName["ok"].GetBool());
}

TEST_CASE("未知命令 / 坏信封") {
    auto h = MakeHost();
    CHECK_FALSE(Parse(ipcrouter::RouteIpc(R"({"id":9,"cmd":"nope","args":{}})", h))["ok"].GetBool());
    CHECK_FALSE(Parse(ipcrouter::RouteIpc("{oops", h))["ok"].GetBool());
    CHECK_FALSE(Parse(ipcrouter::RouteIpc(R"({"cmd":"getConfig","args":{}})", h))["ok"].GetBool()); // 缺 id 拒绝
    // 缺 id 的回复 id 为 -1，JS 侧可丢弃
    CHECK(Parse(ipcrouter::RouteIpc(R"({"cmd":"getConfig"})", h))["id"].GetInt() == -1);
}

TEST_CASE("host 未接线 -> not wired error 且不崩") {
    ipcrouter::IpcHost h;  // 全部 std::function 为空
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":10,"cmd":"getConfig"})", h));
    CHECK_FALSE(r["ok"].GetBool());
    CHECK(std::string(r["error"].GetString()).find("not wired") != std::string::npos);
}

TEST_CASE("host 抛异常 -> internal 且合法 JSON") {
    ipcrouter::IpcHost h;
    h.getConfig = []() -> ipcrouter::HostResult { throw std::runtime_error("boom"); };
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":12,"cmd":"getConfig"})", h));
    CHECK_FALSE(r["ok"].GetBool());
    CHECK(std::string(r["error"].GetString()) == "internal");
}

TEST_CASE("host resultJson 非法 -> 降级失败，不写畸形 JSON") {
    ipcrouter::IpcHost h;
    h.getConfig = [] { ipcrouter::HostResult r; r.ok = true; r.resultJson = "{oops"; return r; };
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":13,"cmd":"getConfig"})", h));
    CHECK_FALSE(r["ok"].GetBool());
    CHECK(std::string(r["error"].GetString()).find("invalid result") != std::string::npos);
}

TEST_CASE("无参命令透传 args") {
    auto h = MakeHost();
    Document r = Parse(ipcrouter::RouteIpc(R"({"id":11,"cmd":"browseProgram","args":{}})", h));
    CHECK(std::string(r["result"]["path"].GetString()).find(".exe") != std::string::npos);
}

#include "doctest.h"
#include "config.h"

#include <windows.h>
#include <fstream>
#include <string>

TEST_CASE("ParseConfig: 空文本返回默认配置") {
    AppConfig cfg; std::string err;
    CHECK(ParseConfig("", cfg, err));
    CHECK(cfg.gesture.dragThreshold == 25.0);
    CHECK(cfg.profiles.size() == 1);
    CHECK(cfg.profiles[0].processName == "Global");
    CHECK(cfg.profiles[0].sectorCount == 8);
    CHECK(cfg.profiles[0].actions.size() == 8);
}

TEST_CASE("ParseConfig: 覆盖字段") {
    const char* j = R"({
      "gesture": {"triggerButton":"x2","dragThreshold":40,"outerEscape":false},
      "appearance": {"wheelRadius":200},
      "profiles": [{"processName":"chrome.exe","sectorCount":4,
        "actions":[{"type":"hotkey","name":"复制","target":"ctrl+c"}]}]
    })";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(j, cfg, err));
    CHECK(cfg.gesture.trigger == TriggerButton::X2);
    CHECK(cfg.gesture.dragThreshold == 40.0);
    CHECK_FALSE(cfg.gesture.outerEscape);
    CHECK(cfg.appearance.wheelRadius == 200.0);
    REQUIRE(cfg.profiles.size() == 2);              // chrome + 自动补的 Global
    CHECK(cfg.profiles[0].actions[0].type == "hotkey");
    CHECK(cfg.profiles[0].actions.size() == 4);     // 1 + 补空 3
}

TEST_CASE("ParseConfig: 坏 JSON 报错不崩") {
    AppConfig cfg; std::string err;
    CHECK_FALSE(ParseConfig("{oops", cfg, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("NormalizeConfig: 钳制与对齐") {
    AppConfig cfg;
    cfg.gesture.dragThreshold = -5;          // -> 10 (下限)
    cfg.profiles[0].sectorCount = 99;        // -> 16 (上限)
    cfg.profiles[0].actions.resize(2);
    NormalizeConfig(cfg);
    CHECK(cfg.gesture.dragThreshold == 10.0);
    CHECK(cfg.profiles[0].sectorCount == 16);
    CHECK(cfg.profiles[0].actions.size() == 16);
}

TEST_CASE("Serialize 再 Parse 保真") {
    AppConfig cfg; std::string err;
    cfg.gesture.dragThreshold = 33;
    cfg.profiles[0].actions[3] = Action{"launch","记事本","notepad.exe",""};
    auto text = SerializeConfig(cfg);
    AppConfig back;
    REQUIRE(ParseConfig(text, back, err));
    CHECK(back.gesture.dragThreshold == 33.0);
    CHECK(back.profiles[0].actions[3].target == "notepad.exe");
}

TEST_CASE("blacklistProcesses: 解析、规范化与序列化往返") {
    const char* j = R"({"gesture":{"blacklistProcesses":["mstsc.exe"]}})";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(j, cfg, err));
    REQUIRE(cfg.gesture.blacklistProcesses.size() == 1);
    CHECK(cfg.gesture.blacklistProcesses[0] == "mstsc.exe");

    // Normalize：去空白 + 小写，丢弃空项
    AppConfig dirty; std::string derr;
    dirty.gesture.blacklistProcesses = {"  MSTSC.exe ", "RDP-WRAPPER.EXE", "   ", ""};
    NormalizeConfig(dirty);
    REQUIRE(dirty.gesture.blacklistProcesses.size() == 2);
    CHECK(dirty.gesture.blacklistProcesses[0] == "mstsc.exe");
    CHECK(dirty.gesture.blacklistProcesses[1] == "rdp-wrapper.exe");

    // Serialize 再 Parse 保真
    auto text = SerializeConfig(cfg);
    AppConfig back;
    REQUIRE(ParseConfig(text, back, err));
    REQUIRE(back.gesture.blacklistProcesses.size() == 1);
    CHECK(back.gesture.blacklistProcesses[0] == "mstsc.exe");
}

TEST_CASE("GetProfileForProcess: 精确匹配与回落") {
    AppConfig cfg; std::string err; ParseConfig("", cfg, err);
    cfg.profiles.push_back(Profile{"chrome", 4, {}});
    CHECK(GetProfileForProcess(cfg, "chrome.exe").processName == "chrome");
    CHECK(GetProfileForProcess(cfg, "notepad.exe").processName == "Global");
}

TEST_CASE("NormalizeConfig: 半配置槽位清洗（type 有但 target 空 -> 空槽）") {
    const char* j = R"({
      "profiles": [{"processName":"Global","sectorCount":4,
        "actions":[{"type":"launch","name":"坏","target":"","args":"x","iconKey":""},
                   {"type":"hotkey","name":"","target":"","args":"","iconKey":""},
                   {"type":"launch","name":"好","target":"notepad.exe","args":"","iconKey":""},
                   {"type":"","name":"","target":"","args":"","iconKey":""}]}]
    })";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(j, cfg, err));
    auto& acts = cfg.profiles[0].actions;
    CHECK(acts[0].type.empty());   // launch+空 target 被清洗
    CHECK(acts[0].name.empty());
    CHECK(acts[0].args.empty());
    CHECK(acts[1].type.empty());   // hotkey+空 target 被清洗
    CHECK(acts[2].type == "launch");  // 有效动作不动
    CHECK(acts[2].target == "notepad.exe");
    CHECK(acts[3].type.empty());
}

TEST_CASE("autoStart: 默认 false，解析/序列化往返保真") {
    AppConfig def; std::string err;
    REQUIRE(ParseConfig("", def, err));
    CHECK_FALSE(def.gesture.autoStart);

    AppConfig cfg;
    REQUIRE(ParseConfig(R"({"gesture":{"autoStart":true}})", cfg, err));
    CHECK(cfg.gesture.autoStart);
    const auto text = SerializeConfig(cfg);
    CHECK(text.find("\"autoStart\": true") != std::string::npos);
    AppConfig back;
    REQUIRE(ParseConfig(text, back, err));
    CHECK(back.gesture.autoStart);
}

TEST_CASE("opacity: 默认 mid，白名单解析，非法值归一化，往返保真") {
    AppConfig def; std::string err;
    REQUIRE(ParseConfig("", def, err));
    CHECK(def.appearance.opacity == "mid");

    AppConfig cfg;
    REQUIRE(ParseConfig(R"({"appearance":{"opacity":"high"}})", cfg, err));
    CHECK(cfg.appearance.opacity == "high");
    const auto text = SerializeConfig(cfg);
    CHECK(text.find("\"opacity\": \"high\"") != std::string::npos);
    AppConfig back;
    REQUIRE(ParseConfig(text, back, err));
    CHECK(back.appearance.opacity == "high");

    // 非法值 -> 归一化为 mid（默认档，行为与 M1 逐位一致）
    AppConfig bad;
    REQUIRE(ParseConfig(R"({"appearance":{"opacity":"banana"}})", bad, err));
    CHECK(bad.appearance.opacity == "mid");
}

TEST_CASE("M3a 兼容: M2 格式(无新字段)加载 -> 全部默认值") {
    const char* m2 = R"({
      "gesture": {"triggerButton":"right","dragThreshold":25.0,"coreRadius":50.0,
        "outerEscape":true,"outerEscapeDistance":186.0,"disableOnModifier":false,
        "disableOnFullScreen":true,"blacklistProcesses":[],"autoStart":false},
      "appearance": {"shape":"classic","theme":"dark","wheelRadius":138.0,
        "innerRadius":52.0,"showLabels":true,"opacity":"mid"},
      "profiles": [{"processName":"Global","sectorCount":8,
        "actions":[{"type":"","name":"","target":"","args":"","iconKey":""}]}]
    })";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(m2, cfg, err));
    CHECK(cfg.gesture.isolationMode == "blacklist");
    CHECK(cfg.gesture.whitelistProcesses.empty());
    CHECK(cfg.appearance.language == "auto");
    CHECK(cfg.appearance.subWheelWidth == 56.0);
    CHECK(cfg.profiles[0].actions[0].subActions.empty());
}

TEST_CASE("M3a subActions: 解析/序列化往返 + 截断至 4") {
    const char* j = R"({
      "profiles":[{"processName":"Global","sectorCount":4,"actions":[
        {"type":"launch","name":"浏览器","target":"chrome.exe","args":"","iconKey":"g:browser",
         "subActions":[
           {"type":"hotkey","name":"新标签","target":"ctrl+t","args":"","iconKey":""},
           {"type":"hotkey","name":"关闭","target":"ctrl+w","args":"","iconKey":""},
           {"type":"hotkey","name":"s1","target":"ctrl+1","args":"","iconKey":""},
           {"type":"hotkey","name":"s2","target":"ctrl+2","args":"","iconKey":""},
           {"type":"hotkey","name":"s3","target":"ctrl+3","args":"","iconKey":""}]}]}]
    })";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(j, cfg, err));
    auto& a = cfg.profiles[0].actions[0];
    CHECK(a.subActions.size() == 4);                    // 5 -> 截断
    CHECK(a.subActions[0].target == "ctrl+t");
    CHECK(a.subActions[3].name == "s2");                // 截断保留前 4
    const auto text = SerializeConfig(cfg);
    CHECK(text.find("\"subActions\"") != std::string::npos);
    AppConfig back;
    REQUIRE(ParseConfig(text, back, err));
    CHECK(back.profiles[0].actions[0].subActions.size() == 4);
    CHECK(back.profiles[0].actions[0].subActions[0].target == "ctrl+t");
    CHECK(back.profiles[0].actions[0].iconKey == "g:browser");
}

TEST_CASE("M3a 子动作清洗: 半配置子槽、子槽再嵌套、空主槽带子槽") {
    const char* j = R"({
      "profiles":[{"processName":"Global","sectorCount":4,"actions":[
        {"type":"launch","name":"x","target":"a.exe","args":"","iconKey":"",
         "subActions":[{"type":"hotkey","name":"","target":"","args":"","iconKey":"",
                        "subActions":[{"type":"launch","name":"n","target":"b.exe","args":"","iconKey":""}]}]},
        {"type":"","name":"","target":"","args":"","iconKey":"",
         "subActions":[{"type":"hotkey","name":"y","target":"ctrl+c","args":"","iconKey":""}]},
        {"type":"launch","name":"z","target":"c.exe","args":"","iconKey":"badicon","subActions":[]},
        {"type":"","name":"","target":"","args":"","iconKey":"","subActions":[]}]}]
    })";
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(j, cfg, err));
    auto& acts = cfg.profiles[0].actions;
    // 槽0：子槽0 半配置 -> 清洗为空槽；其嵌套 subActions 被清空（只递归一层）
    CHECK(acts[0].subActions[0].type.empty());
    CHECK(acts[0].subActions[0].subActions.empty());
    // 槽1：主槽空但子动作合法 -> 保留（M3c 修正：空主槽+子动作是合法配置，
    // 主扇区不触发动作、仅作为子环入口）
    REQUIRE(acts[1].subActions.size() == 1);
    CHECK(acts[1].subActions[0].target == "ctrl+c");
    // 槽2：iconKey 非法前缀 -> 清空
    CHECK(acts[2].iconKey.empty());
}

TEST_CASE("M3a iconKey 语义: g:/f: 前缀保留，其余清空") {
    AppConfig cfg; std::string err;
    const char* j = R"({"profiles":[{"processName":"Global","sectorCount":4,"actions":[
      {"type":"launch","name":"a","target":"x","args":"","iconKey":"g:copy"},
      {"type":"launch","name":"b","target":"y","args":"","iconKey":"f:my.png"},
      {"type":"launch","name":"c","target":"z","args":"","iconKey":"banana"},
      {"type":"launch","name":"d","target":"w","args":"","iconKey":""}]}]})";
    REQUIRE(ParseConfig(j, cfg, err));
    auto& a = cfg.profiles[0].actions;
    CHECK(a[0].iconKey == "g:copy");
    CHECK(a[1].iconKey == "f:my.png");
    CHECK(a[2].iconKey.empty());
    CHECK(a[3].iconKey.empty());
}

TEST_CASE("iconSize: 默认 28，钳制 [16,48]") {
    AppConfig def; std::string err;
    REQUIRE(ParseConfig("", def, err));
    CHECK(def.appearance.iconSize == 28.0);
    AppConfig big;
    REQUIRE(ParseConfig(R"({"appearance":{"iconSize":999}})", big, err));
    CHECK(big.appearance.iconSize == 48.0);
    AppConfig lowv;
    REQUIRE(ParseConfig(R"({"appearance":{"iconSize":5}})", lowv, err));
    CHECK(lowv.appearance.iconSize == 16.0);
    AppConfig back;
    REQUIRE(ParseConfig(SerializeConfig(def), back, err));
    CHECK(back.appearance.iconSize == 28.0);
}

TEST_CASE("labelMode: 默认 all，白名单 all/selected，往返保真") {
    AppConfig def; std::string err;
    REQUIRE(ParseConfig("", def, err));
    CHECK(def.appearance.labelMode == "all");
    AppConfig sel;
    REQUIRE(ParseConfig(R"({"appearance":{"labelMode":"selected"}})", sel, err));
    CHECK(sel.appearance.labelMode == "selected");
    AppConfig bad;
    REQUIRE(ParseConfig(R"({"appearance":{"labelMode":"banana"}})", bad, err));
    CHECK(bad.appearance.labelMode == "all");
    AppConfig back;
    REQUIRE(ParseConfig(SerializeConfig(sel), back, err));
    CHECK(back.appearance.labelMode == "selected");
}

TEST_CASE("M3a 隔离模式与白名单") {
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(R"({"gesture":{"isolationMode":"whitelist","whitelistProcesses":[" Chrome.exe ","","x"]}})", cfg, err));
    CHECK(cfg.gesture.isolationMode == "whitelist");
    REQUIRE(cfg.gesture.whitelistProcesses.size() == 2);
    CHECK(cfg.gesture.whitelistProcesses[0] == "chrome.exe");
    // 未知值回 blacklist
    REQUIRE(ParseConfig(R"({"gesture":{"isolationMode":"ban"}})", cfg, err));
    CHECK(cfg.gesture.isolationMode == "blacklist");
}

TEST_CASE("M3a shape/theme/language/子环宽度 白名单与钳制") {
    AppConfig cfg; std::string err;
    REQUIRE(ParseConfig(R"({"appearance":{"shape":"disc","theme":"system","language":"en","subWheelWidth":200}})", cfg, err));
    CHECK(cfg.appearance.shape == "disc");
    CHECK(cfg.appearance.theme == "system");
    CHECK(cfg.appearance.language == "en");
    CHECK(cfg.appearance.subWheelWidth == 90.0);   // 钳上限
    REQUIRE(ParseConfig(R"({"appearance":{"shape":"hex","theme":"banana","language":"jp","subWheelWidth":-5}})", cfg, err));
    CHECK(cfg.appearance.shape == "classic");
    CHECK(cfg.appearance.theme == "dark");
    CHECK(cfg.appearance.language == "auto");
    CHECK(cfg.appearance.subWheelWidth == 30.0);   // 钳下限
}

TEST_CASE("QuarantineConfigFileAt: 重命名为 .bad-* / 缺文件返回 false 不崩溃") {
    wchar_t tmpDir[MAX_PATH] = {};
    REQUIRE(GetTempPathW(MAX_PATH, tmpDir) > 0);
    const std::wstring base = std::wstring(tmpDir) + L"yipie-test-quarantine.json";
    ::DeleteFileW(base.c_str());  // 清残留

    // 正常路径：存在文件 -> true，原文件消失，.bad-* 出现
    {
        std::ofstream f(base, std::ios::binary);
        f << "{oops";
        f.close();
        REQUIRE(GetFileAttributesW(base.c_str()) != INVALID_FILE_ATTRIBUTES);
        std::string err;
        CHECK(QuarantineConfigFileAt(base, err));
        CHECK(err.empty());
        CHECK(GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES);
        // 找到 .bad- 备份（前缀匹配枚举），验证存在后清理
        WIN32_FIND_DATAW fd{};
        const std::wstring pat = base + L".bad-*";
        HANDLE hq = FindFirstFileW(pat.c_str(), &fd);
        REQUIRE(hq != INVALID_HANDLE_VALUE);
        do {
            ::DeleteFileW((std::wstring(tmpDir) + fd.cFileName).c_str());
        } while (FindNextFileW(hq, &fd));
        FindClose(hq);
    }

    // 缺文件路径：false + err，不崩溃
    {
        std::string err;
        CHECK_FALSE(QuarantineConfigFileAt(base, err));
        CHECK_FALSE(err.empty());
    }
    ::DeleteFileW(base.c_str());
}

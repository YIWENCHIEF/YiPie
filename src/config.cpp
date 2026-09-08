// Task 1: 配置模块实现（解析/序列化/归一化/文件层/Profile 匹配）
#include "config.h"

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#ifndef NOMINMAX
#define NOMINMAX  // windows.h 的 min/max 宏会破坏 rapidjson 的 numeric_limits::(min)()
#endif
#include <windows.h>
#include <shlobj.h>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"

using rapidjson::Document;
using rapidjson::SizeType;
using rapidjson::StringBuffer;
using rapidjson::Value;

// —— 公共转换（config.h 声明）：必须位于匿名空间之外 ——
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                        static_cast<int>(w.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                          s.data(), n, nullptr, nullptr);
    return s;
}

namespace {

constexpr const char* kGlobalName = "Global";

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 去掉末尾的扩展名（含点），用于进程名匹配
std::string StripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return name;
    return name.substr(0, dot);
}

// %APPDATA%\YiPie 目录（不存在则创建）。失败返回空串。
std::wstring ConfigDir() {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base)))
        return {};
    std::wstring dir = std::wstring(base) + L"\\YiPie";
    const DWORD attr = ::GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!::CreateDirectoryW(dir.c_str(), nullptr)) {
            const DWORD e = ::GetLastError();
            if (e != ERROR_ALREADY_EXISTS) return {};
        }
    } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return {};
    }
    return dir;
}

double Clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// 去首尾 ASCII 空白
std::string TrimAscii(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

template <typename T>
T ClampT(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---- JSON 读取辅助：字段缺失/类型不符时保留默认值 ----

bool GetDouble(const Value& obj, const char* key, double& out) {
    if (!obj.HasMember(key)) return false;
    const Value& v = obj[key];
    if (!v.IsNumber()) return false;
    out = v.GetDouble();
    return true;
}

bool GetBool(const Value& obj, const char* key, bool& out) {
    if (!obj.HasMember(key)) return false;
    const Value& v = obj[key];
    if (!v.IsBool()) return false;
    out = v.GetBool();
    return true;
}

bool GetString(const Value& obj, const char* key, std::string& out) {
    if (!obj.HasMember(key)) return false;
    const Value& v = obj[key];
    if (!v.IsString()) return false;
    out.assign(v.GetString(), v.GetStringLength());
    return true;
}

bool GetInt(const Value& obj, const char* key, int& out) {
    if (!obj.HasMember(key)) return false;
    const Value& v = obj[key];
    if (!v.IsNumber()) return false;
    out = static_cast<int>(v.GetDouble());
    return true;
}

TriggerButton ParseTrigger(const std::string& s) {
    const std::string t = ToLowerAscii(s);
    if (t == "middle") return TriggerButton::Middle;
    if (t == "x1") return TriggerButton::X1;
    if (t == "x2") return TriggerButton::X2;
    return TriggerButton::Right;  // "right" 与未知值
}

const char* TriggerToString(TriggerButton b) {
    switch (b) {
        case TriggerButton::Middle: return "middle";
        case TriggerButton::X1: return "x1";
        case TriggerButton::X2: return "x2";
        case TriggerButton::Right:
        default: return "right";
    }
}

// 解析动作数组；depth 0 = 主槽（解析 subActions 一层），depth 1 = 子槽（忽略嵌套键）
void ReadActions(const Value& arr, std::vector<Action>& out, int depth) {
    for (SizeType k = 0; k < arr.Size(); ++k) {
        const Value& av = arr[k];
        if (!av.IsObject()) continue;
        Action a;
        GetString(av, "type", a.type);
        GetString(av, "name", a.name);
        GetString(av, "target", a.target);
        GetString(av, "args", a.args);
        GetString(av, "iconKey", a.iconKey);
        if (depth == 0 && av.HasMember("subActions") && av["subActions"].IsArray())
            ReadActions(av["subActions"], a.subActions, 1);
        out.push_back(std::move(a));
    }
}

void WriteAction(rapidjson::PrettyWriter<StringBuffer>& w, const Action& a) {
    w.StartObject();
    w.Key("type"); w.String(a.type.data(), static_cast<SizeType>(a.type.size()));
    w.Key("name"); w.String(a.name.data(), static_cast<SizeType>(a.name.size()));
    w.Key("target"); w.String(a.target.data(), static_cast<SizeType>(a.target.size()));
    w.Key("args"); w.String(a.args.data(), static_cast<SizeType>(a.args.size()));
    w.Key("iconKey"); w.String(a.iconKey.data(), static_cast<SizeType>(a.iconKey.size()));
    w.Key("subActions");
    w.StartArray();
    for (const Action& sa : a.subActions) WriteAction(w, sa);
    w.EndArray();
    w.EndObject();
}

}  // namespace

void NormalizeConfig(AppConfig& cfg) {
    // gesture 钳制
    cfg.gesture.dragThreshold = Clampd(cfg.gesture.dragThreshold, 10.0, 60.0);
    cfg.gesture.coreRadius = Clampd(cfg.gesture.coreRadius, 15.0, 120.0);
    // outerEscapeDistance：<=0 视为自动（0），否则钳制 [140,320]
    if (cfg.gesture.outerEscapeDistance <= 0.0) cfg.gesture.outerEscapeDistance = 0.0;
    else cfg.gesture.outerEscapeDistance = Clampd(cfg.gesture.outerEscapeDistance, 140.0, 320.0);

    // blacklistProcesses：去空白 + 小写规范化，丢弃空项（钩子内做精确匹配）
    {
        std::vector<std::string> cleaned;
        for (const std::string& s : cfg.gesture.blacklistProcesses) {
            std::string t = ToLowerAscii(TrimAscii(s));
            if (!t.empty()) cleaned.push_back(std::move(t));
        }
        cfg.gesture.blacklistProcesses = std::move(cleaned);
    }

    // appearance 钳制
    cfg.appearance.wheelRadius = Clampd(cfg.appearance.wheelRadius, 80.0, 200.0);
    cfg.appearance.innerRadius =
        Clampd(cfg.appearance.innerRadius, 0.0, cfg.appearance.wheelRadius - 20.0);
    // opacity 白名单：非 low/high 一律回 mid（默认档 = M1 现状 alpha 0.85）
    if (cfg.appearance.opacity != "low" && cfg.appearance.opacity != "high")
        cfg.appearance.opacity = "mid";
    // M3a：隔离模式/白名单清洗（与 blacklistProcesses 同规则）
    if (cfg.gesture.isolationMode != "whitelist") cfg.gesture.isolationMode = "blacklist";
    {
        std::vector<std::string> wcleaned;
        for (const std::string& w : cfg.gesture.whitelistProcesses) {
            std::string t = ToLowerAscii(TrimAscii(w));
            if (!t.empty()) wcleaned.push_back(std::move(t));
        }
        cfg.gesture.whitelistProcesses = std::move(wcleaned);
    }
    // M3a：外观白名单/钳制
    if (cfg.appearance.shape != "disc") cfg.appearance.shape = "classic";
    if (cfg.appearance.theme != "light" && cfg.appearance.theme != "system")
        cfg.appearance.theme = "dark";
    if (cfg.appearance.language != "zh" && cfg.appearance.language != "en")
        cfg.appearance.language = "auto";
    cfg.appearance.subWheelWidth = Clampd(cfg.appearance.subWheelWidth, 30.0, 90.0);
    cfg.appearance.iconSize = Clampd(cfg.appearance.iconSize, 16.0, 48.0);
    if (cfg.appearance.labelMode != "selected") cfg.appearance.labelMode = "all";

    // profiles：保证至少一个、且包含 Global
    if (cfg.profiles.empty()) cfg.profiles.push_back(Profile{});
    {
        const bool hasGlobal = std::any_of(
            cfg.profiles.begin(), cfg.profiles.end(),
            [](const Profile& p) { return p.processName == kGlobalName; });
        if (!hasGlobal) cfg.profiles.push_back(Profile{});
    }
    // sectorCount 钳制 [3,16] + actions 补齐/截断到 sectorCount
    for (Profile& p : cfg.profiles) {
        p.sectorCount = ClampT<int>(p.sectorCount, 3, 16);
        p.actions.resize(static_cast<size_t>(p.sectorCount));
        // 半配置槽位清洗：type 非空但 target 空 = 不可执行（旧版竞态可能留下
        // 这类槽位；launch 空 target 会让 ShellExecute 打开默认文件夹）。
        // 统一还原为空槽，name/args/iconKey 一并清掉。M3a 扩展：iconKey 前缀
        // 白名单、子动作清洗/截断、空主槽清子槽。
        auto cleanIcon = [](std::string& ic) {
            if (!ic.empty() && ic.rfind("g:", 0) != 0 && ic.rfind("f:", 0) != 0) ic.clear();
        };
        for (Action& a : p.actions) {
            if (!a.type.empty() && a.target.empty()) {
                a = Action{};
            }
            cleanIcon(a.iconKey);
            if (a.subActions.size() > 4) a.subActions.resize(4);
            for (Action& sa : a.subActions) {
                if (!sa.type.empty() && sa.target.empty()) sa = Action{};
                cleanIcon(sa.iconKey);
                sa.subActions.clear();  // 子动作不再嵌套（解析只递归一层，此处兜底）
            }
        }
    }
}

bool ParseConfig(const std::string& jsonText, AppConfig& out, std::string& err) {
    err.clear();
    if (jsonText.empty()) {
        out = AppConfig();
        NormalizeConfig(out);
        return true;
    }

    Document doc;
    doc.Parse<rapidjson::kParseDefaultFlags>(jsonText.data(), jsonText.size());
    if (doc.HasParseError()) {
        std::ostringstream os;
        os << "JSON parse error at offset " << doc.GetErrorOffset() << ": "
           << rapidjson::GetParseError_En(doc.GetParseError());
        err = os.str();
        return false;
    }
    if (!doc.IsObject()) {
        err = "JSON root must be an object";
        return false;
    }

    AppConfig cfg;  // 默认值起步：缺失字段保留默认
    cfg.profiles.clear();  // profiles 完全以 JSON 为准（缺省则走下面的 Global 兜底）

    if (doc.HasMember("gesture") && doc["gesture"].IsObject()) {
        const Value& g = doc["gesture"];
        std::string trig;
        if (GetString(g, "triggerButton", trig)) cfg.gesture.trigger = ParseTrigger(trig);
        GetDouble(g, "dragThreshold", cfg.gesture.dragThreshold);
        GetDouble(g, "coreRadius", cfg.gesture.coreRadius);
        GetBool(g, "outerEscape", cfg.gesture.outerEscape);
        GetDouble(g, "outerEscapeDistance", cfg.gesture.outerEscapeDistance);
        GetBool(g, "disableOnModifier", cfg.gesture.disableOnModifier);
        GetBool(g, "disableOnFullScreen", cfg.gesture.disableOnFullScreen);
        GetBool(g, "autoStart", cfg.gesture.autoStart);
        GetString(g, "isolationMode", cfg.gesture.isolationMode);
        if (g.HasMember("whitelistProcesses") && g["whitelistProcesses"].IsArray()) {
            const Value& warr = g["whitelistProcesses"];
            cfg.gesture.whitelistProcesses.clear();
            for (SizeType i = 0; i < warr.Size(); ++i) {
                if (!warr[i].IsString()) continue;
                cfg.gesture.whitelistProcesses.emplace_back(warr[i].GetString(),
                                                            warr[i].GetStringLength());
            }
        }
        if (g.HasMember("blacklistProcesses") && g["blacklistProcesses"].IsArray()) {
            const Value& arr = g["blacklistProcesses"];
            cfg.gesture.blacklistProcesses.clear();
            for (SizeType i = 0; i < arr.Size(); ++i) {
                if (!arr[i].IsString()) continue;  // 非字符串元素直接丢弃
                cfg.gesture.blacklistProcesses.emplace_back(arr[i].GetString(),
                                                            arr[i].GetStringLength());
            }
        }
    }

    if (doc.HasMember("appearance") && doc["appearance"].IsObject()) {
        const Value& a = doc["appearance"];
        GetString(a, "shape", cfg.appearance.shape);
        GetString(a, "theme", cfg.appearance.theme);
        GetDouble(a, "wheelRadius", cfg.appearance.wheelRadius);
        GetDouble(a, "innerRadius", cfg.appearance.innerRadius);
        GetBool(a, "showLabels", cfg.appearance.showLabels);
        GetString(a, "opacity", cfg.appearance.opacity);
        GetString(a, "language", cfg.appearance.language);
        GetDouble(a, "subWheelWidth", cfg.appearance.subWheelWidth);
        GetDouble(a, "iconSize", cfg.appearance.iconSize);
        GetString(a, "labelMode", cfg.appearance.labelMode);
    }

    if (doc.HasMember("profiles") && doc["profiles"].IsArray()) {
        const Value& arr = doc["profiles"];
        for (SizeType i = 0; i < arr.Size(); ++i) {
            const Value& v = arr[i];
            if (!v.IsObject()) continue;
            Profile p;
            GetString(v, "processName", p.processName);
            GetInt(v, "sectorCount", p.sectorCount);
            if (v.HasMember("actions") && v["actions"].IsArray())
                ReadActions(v["actions"], p.actions, 0);
            cfg.profiles.push_back(std::move(p));
        }
    }

    // 无 Global 则自动追加，然后归一化
    {
        const bool hasGlobal = std::any_of(
            cfg.profiles.begin(), cfg.profiles.end(),
            [](const Profile& pr) { return pr.processName == kGlobalName; });
        if (!hasGlobal) cfg.profiles.push_back(Profile{});
    }
    NormalizeConfig(cfg);
    out = std::move(cfg);
    return true;
}

std::string SerializeConfig(const AppConfig& cfg) {
    StringBuffer sb;
    rapidjson::PrettyWriter<StringBuffer> w(sb);
    w.SetIndent(' ', 2);  // 缩进 2 空格

    w.StartObject();

    w.Key("gesture");
    w.StartObject();
    w.Key("triggerButton"); w.String(TriggerToString(cfg.gesture.trigger));
    w.Key("dragThreshold"); w.Double(cfg.gesture.dragThreshold);
    w.Key("coreRadius"); w.Double(cfg.gesture.coreRadius);
    w.Key("outerEscape"); w.Bool(cfg.gesture.outerEscape);
    w.Key("outerEscapeDistance"); w.Double(cfg.gesture.outerEscapeDistance);
    w.Key("disableOnModifier"); w.Bool(cfg.gesture.disableOnModifier);
    w.Key("disableOnFullScreen"); w.Bool(cfg.gesture.disableOnFullScreen);
    w.Key("autoStart"); w.Bool(cfg.gesture.autoStart);
    w.Key("isolationMode"); w.String(cfg.gesture.isolationMode.data(), static_cast<SizeType>(cfg.gesture.isolationMode.size()));
    w.Key("whitelistProcesses");
    w.StartArray();
    for (const std::string& item : cfg.gesture.whitelistProcesses)
        w.String(item.data(), static_cast<SizeType>(item.size()));
    w.EndArray();
    w.Key("blacklistProcesses");
    w.StartArray();
    for (const std::string& s : cfg.gesture.blacklistProcesses)
        w.String(s.data(), static_cast<SizeType>(s.size()));
    w.EndArray();
    w.EndObject();

    w.Key("appearance");
    w.StartObject();
    w.Key("shape"); w.String(cfg.appearance.shape.data(), static_cast<SizeType>(cfg.appearance.shape.size()));
    w.Key("theme"); w.String(cfg.appearance.theme.data(), static_cast<SizeType>(cfg.appearance.theme.size()));
    w.Key("wheelRadius"); w.Double(cfg.appearance.wheelRadius);
    w.Key("innerRadius"); w.Double(cfg.appearance.innerRadius);
    w.Key("showLabels"); w.Bool(cfg.appearance.showLabels);
    w.Key("opacity"); w.String(cfg.appearance.opacity.data(), static_cast<SizeType>(cfg.appearance.opacity.size()));
    w.Key("language"); w.String(cfg.appearance.language.data(), static_cast<SizeType>(cfg.appearance.language.size()));
    w.Key("subWheelWidth"); w.Double(cfg.appearance.subWheelWidth);
    w.Key("iconSize"); w.Double(cfg.appearance.iconSize);
    w.Key("labelMode"); w.String(cfg.appearance.labelMode.data(), static_cast<SizeType>(cfg.appearance.labelMode.size()));
    w.EndObject();

    w.Key("profiles");
    w.StartArray();
    for (const Profile& p : cfg.profiles) {
        w.StartObject();
        w.Key("processName"); w.String(p.processName.data(), static_cast<SizeType>(p.processName.size()));
        w.Key("sectorCount"); w.Int(p.sectorCount);
        w.Key("actions");
        w.StartArray();
        for (const Action& a : p.actions) WriteAction(w, a);
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();

    w.EndObject();
    return std::string(sb.GetString(), sb.GetSize());
}

bool LoadConfig(AppConfig& out, std::string& err) {
    err.clear();
    const std::wstring dir = ConfigDir();
    if (dir.empty()) {
        err = "cannot resolve %APPDATA%\\YiPie directory";
        return false;
    }
    const std::wstring path = dir + L"\\config.json";

    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // 文件不存在：写入默认配置后返回
        out = AppConfig();
        NormalizeConfig(out);
        std::string saveErr;
        if (!SaveConfig(out, saveErr)) {
            // 写入失败不影响本次加载（仍返回默认配置），仅在 err 中提示
            err = "config.json missing; defaults in-memory only: " + saveErr;
        }
        return true;
    }

    // 读取带重试：杀软/索引器常在文件刚被替换后短暂排他锁住它（历史 bug：
    // 一次打开失败被上层误判"配置损坏" -> 隔离覆盖 -> 用户热键配置丢失）。
    // 重试 ~1.2s 仍失败则报 io-transient（上层保留内存配置、绝不动磁盘）。
    std::string text;
    bool readOk = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::ifstream fr(std::wstring(path), std::ios::binary);
        if (fr) {
            std::ostringstream ss;
            ss << fr.rdbuf();
            if (!fr.bad()) { text = ss.str(); readOk = true; break; }
        }
        ::Sleep(150);
    }
    if (!readOk) {
        err = "io-transient: cannot open " + WideToUtf8(path);
        return false;
    }
    if (text.empty()) {
        // 空文件视为默认配置
        out = AppConfig();
        NormalizeConfig(out);
        return true;
    }
    if (!ParseConfig(text, out, err)) return false;
    // 自愈回写：归一化/半配置清洗可能改动了内存值（如旧竞态残留的
    // type有-target空 槽位）。写回磁盘让文件与运行时一致；失败不影响本次加载
    // （运行时防护已独立生效）。失败时写 sidecar + 诊断日志，解开「应用内不重写」悬案。
    if (SerializeConfig(out) != text) {
        std::string saveErr;
        if (SaveConfig(out, saveErr)) {
            ::DeleteFileW((dir + L"\\config.json.new").c_str());  // 幂等清理旧 sidecar
        } else {
            std::string altErr;
            const bool altOk = SaveConfigFileAt(dir + L"\\config.json.new", out, altErr);
            wchar_t tmpPath[MAX_PATH] = {};
            if (GetTempPathW(MAX_PATH, tmpPath)) {
                std::ofstream dbg(std::wstring(tmpPath) + L"\\yipie-config-write.log",
                                  std::ios::app | std::ios::binary);
                if (dbg)
                    dbg << "selfheal-fail err=" << saveErr
                        << " sidecar=" << (altOk ? std::string("ok") : ("fail:" + altErr))
                        << std::endl;
            }
        }
    }
    return true;
}

bool SaveConfigFileAt(const std::wstring& path, const AppConfig& cfg, std::string& err) {
    err.clear();
    if (path.empty()) { err = "empty path"; return false; }
    const std::wstring tmp = path + L".tmp";
    const std::string text = SerializeConfig(cfg);
    {
        std::ofstream f(std::wstring(tmp), std::ios::binary | std::ios::trunc);
        if (!f) {
            err = "cannot write " + WideToUtf8(tmp);
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));  // UTF-8 无 BOM
        f.flush();
        if (!f) {
            err = "write failed for " + WideToUtf8(tmp);
            return false;
        }
    }
    // 目标文件常被杀软扫描短暂锁住（实测 GLE=5），重试可消化大部分瞬时失败
    BOOL moved = FALSE;
    DWORD gle = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        moved = ::MoveFileExW(tmp.c_str(), path.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (moved) break;
        gle = ::GetLastError();
        ::Sleep(200);
    }
    if (!moved) {
        err = "MoveFileExW failed,GLE=" + std::to_string(gle);
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool LoadConfigFileAt(const std::wstring& path, AppConfig& out, std::string& err) {
    err.clear();
    if (path.empty() || ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "file not found: " + WideToUtf8(path);
        return false;
    }
    std::ifstream f(std::wstring(path), std::ios::binary);
    if (!f) {
        err = "cannot open " + WideToUtf8(path);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ParseConfig(ss.str(), out, err);
}

bool SaveConfig(const AppConfig& cfg, std::string& err) {
    const std::wstring path = ConfigFilePath();
    if (path.empty()) {
        err = "cannot resolve %APPDATA%\\YiPie directory";
        return false;
    }
    return SaveConfigFileAt(path, cfg, err);
}

std::wstring ConfigFilePath() {
    const std::wstring dir = ConfigDir();
    if (dir.empty()) return {};
    return dir + L"\\config.json";
}

bool QuarantineConfigFileAt(const std::wstring& path, std::string& err) {
    err.clear();
    if (path.empty()) {
        err = "quarantine: empty path";
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        err = "quarantine: file not found: " + WideToUtf8(path) +
              ",GLE=" + std::to_string(::GetLastError());
        return false;
    }
    // path + ".bad-" + YYYYMMDDHHMMSS（本地时间）
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t stamp[32] = {};
    ::swprintf(stamp, 32, L"%04u%02u%02u%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    const std::wstring dst = path + L".bad-" + stamp;
    if (!::MoveFileExW(path.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        err = "quarantine: MoveFileExW failed,GLE=" + std::to_string(::GetLastError());
        return false;
    }
    return true;
}

Profile& GetProfileForProcess(AppConfig& cfg, const std::string& processName) {
    const std::string want = ToLowerAscii(StripExtension(processName));
    for (Profile& p : cfg.profiles) {
        if (ToLowerAscii(StripExtension(p.processName)) == want) return p;
    }
    // 回落到 Global（LoadConfig/ParseConfig 保证其存在；此处兜底防御）
    for (Profile& p : cfg.profiles) {
        if (p.processName == kGlobalName) return p;
    }
    cfg.profiles.push_back(Profile{});
    NormalizeConfig(cfg);
    return cfg.profiles.back();
}

#pragma once
#include <string>
#include <vector>

// —— M2 T2: UTF-8 <-> UTF-16 公共转换（config.cpp 实现，宿主/UI 共用）——
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& w);

enum class TriggerButton { Right, Middle, X1, X2 };

struct GestureSettings {
    TriggerButton trigger = TriggerButton::Right;
    double dragThreshold = 25.0;
    double coreRadius = 50.0;
    bool outerEscape = true;
    double outerEscapeDistance = 186.0;   // <=0 时引擎按 wheelRadius*1.5 计算
    bool disableOnModifier = false;
    bool disableOnFullScreen = true;
    // 进程隔离黑名单：exe 基名（含 .exe），Normalize 后为小写去空白；钩子内做小写精确匹配
    std::vector<std::string> blacklistProcesses;
    // 开机自启：true=写入 HKCU Run 值「YiPie」，false=删除。启动与重载后由
    // SyncAutoStart 同步到注册表（M2 将作为设置页开关）。
    bool autoStart = false;
    // 进程隔离模式："blacklist"（默认，名单内不触发）| "whitelist"（仅名单内触发）
    std::string isolationMode = "blacklist";
    std::vector<std::string> whitelistProcesses;
};

struct AppearanceSettings {
    std::string shape = "classic";
    std::string theme = "dark";
    double wheelRadius = 138.0;
    double innerRadius = 52.0;
    bool showLabels = true;
    std::string opacity = "mid";   // low/mid/high -> alpha 乘数 0.55/0.85/1.0（渲染器 OpacityMul）
    std::string language = "auto";     // "auto"(=zh) | "zh" | "en"（M3d 英文界面）
    double subWheelWidth = 56.0;       // 二级子环宽度 px，钳制 [30,90]（M3b）
    double iconSize = 28.0;            // 扇区图标边长 px，钳制 [16,48]（M3c）
    std::string labelMode = "all";     // "all"=始终显示标签 | "selected"=仅选中扇区显示（M3d）
};

struct Action {
    std::string type;     // ""(空槽) | "launch" | "hotkey"
    std::string name;
    std::string target;   // launch: 路径/URL; hotkey: "ctrl+alt+t"
    std::string args;     // launch 命令行参数
    std::string iconKey;  // ""=自动配图 | "g:<id>"=字形库 | "f:<file>"=导入图标（M3a 语义）
    std::vector<Action> subActions;  // 0~4 个二级子动作；子动作自身不再嵌套（M3b）
};

struct Profile {
    std::string processName = "Global";
    int sectorCount = 8;
    std::vector<Action> actions;
};

// 默认 AppConfig 自带一个填满空槽的 Global profile
// （brief 的 Normalize/Serialize 用例直接索引 profiles[0].actions[3]）
inline std::vector<Profile> DefaultProfiles() {
    Profile p;
    p.actions.resize(static_cast<size_t>(p.sectorCount));
    std::vector<Profile> v;
    v.push_back(p);
    return v;
}

struct AppConfig {
    GestureSettings gesture;
    AppearanceSettings appearance;
    std::vector<Profile> profiles = DefaultProfiles();
};

// 纯逻辑、可测试：从 JSON 文本解析（失败返回 false 并填 err）
bool ParseConfig(const std::string& jsonText, AppConfig& out, std::string& err);
// 序列化（含全部默认值字段，便于用户手改）
std::string SerializeConfig(const AppConfig& cfg);
// 钳制非法值 + 每个 profile 的 actions 补齐/截断到 sectorCount
void NormalizeConfig(AppConfig& cfg);
// 文件层：路径在 %APPDATA%\YiPie\config.json；文件不存在时写入默认配置
bool LoadConfig(AppConfig& out, std::string& err);
// 原子保存：写 config.json.tmp 后 MoveFileExW(REPLACE_EXISTING)
bool SaveConfig(const AppConfig& cfg, std::string& err);
// 指定路径的同款原子写 / 指定路径的读取+解析（导入导出对话框用）。
// LoadConfigFileAt 对空/缺失文件的处理与 LoadConfig 不同：一律报失败（导入场景）。
bool SaveConfigFileAt(const std::wstring& path, const AppConfig& cfg, std::string& err);
bool LoadConfigFileAt(const std::wstring& path, AppConfig& out, std::string& err);
// 配置文件完整路径（%APPDATA%\YiPie\config.json）；目录解析失败返回空串
std::wstring ConfigFilePath();
// 规格 §4：隔离坏配置文件 —— 把 path 重命名为 path + ".bad-" + YYYYMMDDHHMMSS。
// 文件不存在或重命名失败返回 false 并填 err（UTF-8），不抛异常、不崩溃。
bool QuarantineConfigFileAt(const std::wstring& path, std::string& err);
// 进程名(不含.exe,大小写不敏感)→ Profile；无匹配返回(或创建) Global
Profile& GetProfileForProcess(AppConfig& cfg, const std::string& processName);

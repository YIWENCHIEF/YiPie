#include "doctest.h"
#include "gesture_engine.h"

static GestureEngine MakeEngine(int n = 8) {
    static GestureSettings g; static AppearanceSettings a;
    a.wheelRadius = 138; g.dragThreshold = 25; g.coreRadius = 50;
    g.outerEscape = true; g.outerEscapeDistance = 300;
    return GestureEngine(g, a, n);
}

TEST_CASE("方向判定: 8 向,东为 0 号,顺时针(y 向下)") {
    auto e = MakeEngine();
    e.OnButtonDown(500, 500);
    e.OnMouseMove(500 + 80, 500);      CHECK(e.sector() == 0);  // 正东
    e.OnMouseMove(500, 500 + 80);      CHECK(e.sector() == 2);  // 正南(screen y+)
    e.OnMouseMove(500 - 80, 500);      CHECK(e.sector() == 4);  // 正西
    e.OnMouseMove(500, 500 - 80);      CHECK(e.sector() == 6);  // 正北
    e.OnMouseMove(500 + 60, 500 - 60); CHECK(e.sector() == 7);  // 东北
    e.OnMouseMove(500 + 80 * 0.924, 500 + 80 * 0.383); CHECK(e.sector() == 1); // 22.5°边界内侧
}

TEST_CASE("方向判定: 4 向与 3 向") {
    auto e4 = MakeEngine(4); e4.OnButtonDown(0,0);
    e4.OnMouseMove(30, -30); CHECK(e4.sector() == 3);  // 上
    e4.OnMouseMove(-30, 30); CHECK(e4.sector() == 1);  // 下
    auto e3 = MakeEngine(3); e3.OnButtonDown(0,0);
    e3.OnMouseMove(100, 0); CHECK(e3.sector() == 0);
    // brief 原文此处用 2.1 rad,但 2.1∈[60°,180°) 在东中心顺时针布局下属于扇区 1,
    // "期望 2"与几何矛盾(任何取整公式都无法同时满足本行与 8/4 向断言)。
    // 改为 4.2 rad(∈[180°,300°) → 扇区 2),保持"下扇区"断言意图。详见 task-2-report。
    e3.OnMouseMove(100 * std::cos(4.2), 100 * std::sin(4.2)); CHECK(e3.sector() == 2);
}

TEST_CASE("阈值: 未越过不激活; 越过后刚激活返回 true") {
    auto e = MakeEngine();
    e.OnButtonDown(100, 100);
    CHECK(e.phase() == Phase::Arming);
    CHECK_FALSE(e.OnMouseMove(115, 100));           // 15px < 25 未激活
    CHECK(e.phase() == Phase::Arming);
    CHECK(e.OnMouseMove(130, 100));                 // 30px 激活
    CHECK(e.phase() == Phase::Active);
    CHECK_FALSE(e.OnMouseMove(131, 100));           // 已激活,不再"刚激活"
    CHECK(e.sector() == 0);
}

TEST_CASE("死区: 松手在核心圆内 Cancel") {
    auto e = MakeEngine();
    e.OnButtonDown(500, 500);
    e.OnMouseMove(530, 500);                        // Active, sector 0
    CHECK(e.OnButtonUp(505, 505) == GestureEngine::Outcome::Cancel);
    // 注意 coreEff = min(50, 15) = 15, 回到距起点 ~7px → 死区
}

TEST_CASE("外甩: 超距进入 escaped, 松手 Cancel") {
    auto e = MakeEngine();
    e.OnButtonDown(500, 500);
    e.OnMouseMove(820, 500);                        // 320px > 300
    CHECK(e.escaped());
    CHECK(e.sector() == -1);
    CHECK(e.OnButtonUp(820, 500) == GestureEngine::Outcome::Cancel);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(600, 500);
    CHECK_FALSE(e.escaped());
}

TEST_CASE("Outcome: Arming 松手 ReplayClick, Active 松手 Fire") {
    auto e = MakeEngine();
    e.OnButtonDown(100, 100);
    CHECK(e.OnButtonUp(101, 101) == GestureEngine::Outcome::ReplayClick);
    e.OnButtonDown(100, 100);
    e.OnMouseMove(180, 100);
    CHECK(e.OnButtonUp(180, 100) == GestureEngine::Outcome::Fire);
    CHECK(e.phase() == Phase::Idle);
}

TEST_CASE("OnAbort 回 Idle 且丢状态") {
    auto e = MakeEngine();
    e.OnButtonDown(100, 100); e.OnMouseMove(180, 100);
    e.OnAbort();
    CHECK(e.phase() == Phase::Idle);
    CHECK(e.sector() == -1);
}

// ===================== M3b: 二级子轮盘判定 =====================

static Profile MakeProfileWithSubs() {
    Profile p; p.sectorCount = 8;
    p.actions.assign(8, Action{});
    p.actions[0].type = "launch"; p.actions[0].target = "a.exe";
    p.actions[0].subActions = {
        Action{"hotkey", "s0", "ctrl+a", "", "", {}},
        Action{"hotkey", "s1", "ctrl+b", "", "", {}},
        Action{"hotkey", "s2", "ctrl+c", "", "", {}},
    };  // 扇区0（东，中线 0°，区间 [-22.5°,+22.5°)）三个子槽各 15°
    p.actions[2].type = "launch"; p.actions[2].target = "c.exe";  // 南，无子动作
    return p;
}
static GestureEngine MakeSubEngine(const Profile& p) {
    static GestureSettings g; static AppearanceSettings a;
    a.wheelRadius = 138; a.subWheelWidth = 56;
    g.dragThreshold = 25; g.coreRadius = 50; g.outerEscape = true; g.outerEscapeDistance = 300;
    return GestureEngine(g, a, p.sectorCount, &p);
}

TEST_CASE("子环: 越过主环进入子环区, 父扇区角度均分命中") {
    auto p = MakeProfileWithSubs();
    auto e = MakeSubEngine(p);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(650, 500);                 // 东 150px: 主扇区0, 子环区 [138,194)
    CHECK(e.sector() == 0);
    CHECK(e.showSubRing());
    CHECK(e.subSector() == 1);               // 0° 在子槽1区间 [-7.5°,7.5°) 内
    e.OnMouseMove(650, 470);                 // 约 -11.3°: 子槽0
    CHECK(e.subSector() == 0);
    e.OnMouseMove(650, 530);                 // 约 +11.3°: 子槽2
    CHECK(e.subSector() == 2);
}

TEST_CASE("子环: 迟滞收起——回到 0.9*wheelRadius 内才关") {
    auto p = MakeProfileWithSubs();
    auto e = MakeSubEngine(p);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(650, 500);
    CHECK(e.showSubRing());
    e.OnMouseMove(630, 500);                 // 130 < 138 但 > 124.2: 仍显示
    CHECK(e.showSubRing());
    CHECK(e.subSector() == -1);              // 子槽命中仅 >=138
    e.OnMouseMove(620, 500);                 // 120 < 124.2: 收起
    CHECK_FALSE(e.showSubRing());
}

TEST_CASE("子环: 无子动作扇区越界 = M2 行为(维持主选中, 无子环)") {
    auto p = MakeProfileWithSubs();
    auto e = MakeSubEngine(p);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(500, 660);                 // 南 160px 越过主环
    CHECK(e.sector() == 2);
    CHECK_FALSE(e.showSubRing());
    CHECK(e.subSector() == -1);
}

TEST_CASE("子环: 外甩联动——子环激活时 escape 半径 = max(cfg, subOuter+20)") {
    auto p = MakeProfileWithSubs();
    static GestureSettings g; static AppearanceSettings a;
    a.wheelRadius = 138; a.subWheelWidth = 56;   // subOuter=194, +20=214
    g.dragThreshold = 25; g.coreRadius = 50; g.outerEscape = true; g.outerEscapeDistance = 150;
    GestureEngine e(g, a, 8, &p);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(680, 500);                     // 180 > cfg150 但 < 214: 子环态不逃逸
    CHECK(e.showSubRing());
    CHECK_FALSE(e.escaped());
    e.OnMouseMove(720, 500);                     // 220 > 214: 逃逸
    CHECK(e.escaped());
    CHECK(e.subSector() == -1);
    // 无子动作扇区：cfg 150 照常生效
    e.OnButtonDown(500, 500);
    e.OnMouseMove(500, 660);                     // 南 160 > 150
    CHECK(e.escaped());
}

TEST_CASE("子环: 松手二级寻址 Fire + subSector 语义") {
    auto p = MakeProfileWithSubs();
    auto e = MakeSubEngine(p);
    e.OnButtonDown(500, 500);
    e.OnMouseMove(650, 500);                     // 子槽1
    CHECK(e.OnButtonUp(650, 500) == GestureEngine::Outcome::Fire);
    CHECK(e.sector() == 0);
    CHECK(e.subSector() == 1);                   // 松手后保留（既有契约）
    // 主环内松手 -> Fire 主扇区, subSector=-1
    e.OnButtonDown(500, 500);
    e.OnMouseMove(580, 500);
    CHECK(e.OnButtonUp(580, 500) == GestureEngine::Outcome::Fire);
    CHECK(e.subSector() == -1);
}

TEST_CASE("子环: nullptr profile = M2 行为逐位不变") {
    static GestureSettings g; static AppearanceSettings a;
    a.wheelRadius = 138; g.dragThreshold = 25; g.coreRadius = 50;
    g.outerEscape = true; g.outerEscapeDistance = 300;
    GestureEngine e(g, a, 8);                    // 第4参缺省
    e.OnButtonDown(0, 0);
    e.OnMouseMove(150, 0);
    CHECK_FALSE(e.showSubRing());
    CHECK(e.subSector() == -1);
}

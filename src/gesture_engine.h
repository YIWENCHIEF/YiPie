#pragma once
#include "config.h"
#include <cmath>

enum class Phase { Idle, Arming, Active };

class GestureEngine {
public:
    enum class Outcome { ReplayClick, Cancel, Fire };
    // profile 非空时启用二级子环判定（M3b）；nullptr = M2 行为逐位不变
    GestureEngine(const GestureSettings& g, const AppearanceSettings& a, int sectorCount,
                  const Profile* profile = nullptr);

    void OnButtonDown(double x, double y);          // Idle -> Arming
    bool OnMouseMove(double x, double y);           // 返回是否发生"本帧刚激活"（Arming->Active）
    Outcome OnButtonUp(double x, double y);         // 任何相位都回到 Idle
    void OnAbort();                                 // 中途场景失效（如按下修饰键），直接回 Idle

    Phase phase() const { return phase_; }
    int sector() const { return sector_; }          // -1 = 无选中
    int subSector() const { return subSector_; }    // 子环选中（-1 无）；松手后保留至下次 Down/Abort
    bool showSubRing() const { return showSub_; }   // 渲染层据此展开/收起子环（含迟滞）
    bool escaped() const { return escaped_; }
    double angle() const { return angle_; }         // 起点->当前光标 方位角[0,2π)，供指针渲染
    double distance() const { return dist_; }
    int sectorCount() const { return n_; }

private:
    void evaluate(double x, double y);
    int parentSubCount(int sector) const;
    const GestureSettings& g_;
    const AppearanceSettings& a_;
    const Profile* prof_;       // 只读；nullptr = 子环逻辑关闭
    int n_;
    double coreEff_;    // max(15, min(coreRadius, 0.6*dragThreshold))
    double escapeDist_; // outerEscape ? (distance>0? distance : wheelRadius*1.5) : +inf
    double subOuter_;   // wheelRadius + max(0, subWheelWidth)
    double hysteresisOff_;  // 子环收起迟滞线 = wheelRadius*0.9
    Phase phase_ = Phase::Idle;
    double sx_ = 0, sy_ = 0;   // 起点
    int sector_ = -1;
    int subSector_ = -1;
    bool showSub_ = false;
    bool escaped_ = false;
    double angle_ = 0, dist_ = 0;
};

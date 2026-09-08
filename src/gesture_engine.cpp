#include "gesture_engine.h"
#include <algorithm>
#include <limits>

// 纯 std 实现,不含任何 windows.h 依赖。
// 屏幕坐标系 y 向下: atan2(dy,dx) ∈ [0,2π) 天然是"从东起顺时针"方位角。
// 扇区 0 中心在东(角度 0),边界在 k±step/2 —— 匹配 8 向约定: 南=+y→2, 北=−y→6, 东北→7。

namespace {
constexpr double kPi = 3.14159265358979323846;
}

GestureEngine::GestureEngine(const GestureSettings& g, const AppearanceSettings& a, int sectorCount,
                             const Profile* profile)
    : g_(g), a_(a), prof_(profile) {
    n_ = std::clamp(sectorCount, 3, 16);
    coreEff_ = std::max(15.0, std::min(g.coreRadius, g.dragThreshold * 0.6));
    escapeDist_ = g.outerEscape
        ? (g.outerEscapeDistance > 0 ? g.outerEscapeDistance : a.wheelRadius * 1.5)
        : std::numeric_limits<double>::infinity();
    subOuter_ = a.wheelRadius + std::max(0.0, a.subWheelWidth);
    hysteresisOff_ = a.wheelRadius * 0.9;
}

int GestureEngine::parentSubCount(int sector) const {
    if (!prof_ || sector < 0 || sector >= static_cast<int>(prof_->actions.size())) return 0;
    return static_cast<int>(prof_->actions[sector].subActions.size());
}

void GestureEngine::OnButtonDown(double x, double y) {
    phase_ = Phase::Arming;
    sx_ = x;
    sy_ = y;
    sector_ = -1;
    subSector_ = -1;
    showSub_ = false;
    escaped_ = false;
    dist_ = 0;
    angle_ = 0;
}

bool GestureEngine::OnMouseMove(double x, double y) {
    if (phase_ == Phase::Idle) return false;

    // Arming 阶段也要先算 dist_/angle_(复用 evaluate 开头), 恰好越阈值的帧让 evaluate 正常出结果
    const double dx = x - sx_;
    const double dy = y - sy_;
    dist_ = std::hypot(dx, dy);
    angle_ = std::atan2(dy, dx);
    if (angle_ < 0) angle_ += 2 * kPi;

    bool justActivated = false;
    if (phase_ == Phase::Arming && dist_ >= g_.dragThreshold) {
        phase_ = Phase::Active;
        justActivated = true;
    }
    if (phase_ == Phase::Active) evaluate(x, y);
    return justActivated;
}

GestureEngine::Outcome GestureEngine::OnButtonUp(double x, double y) {
    if (phase_ == Phase::Active) evaluate(x, y);  // 以松手坐标为准
    Outcome r = (phase_ == Phase::Arming) ? Outcome::ReplayClick
              : (phase_ == Phase::Active && sector_ >= 0 && !escaped_) ? Outcome::Fire
              : Outcome::Cancel;
    // Task 7 接线修正：sector_/escaped_ 在松手后不清零——简报的宿主用法是
    // Fire 之后读 engine.sector() 取槽位动作（profile.actions[engine.sector()]）。
    // 清零会使宿主永远读到 -1。下一次 OnButtonDown / OnAbort 会完整重置状态。
    phase_ = Phase::Idle;
    return r;
}

void GestureEngine::OnAbort() {
    phase_ = Phase::Idle;
    sector_ = -1;
    subSector_ = -1;
    showSub_ = false;
    escaped_ = false;
    dist_ = 0;
    angle_ = 0;
}

void GestureEngine::evaluate(double x, double y) {
    const double dx = x - sx_;
    const double dy = y - sy_;
    dist_ = std::hypot(dx, dy);
    angle_ = std::atan2(dy, dx);
    if (angle_ < 0) angle_ += 2 * kPi;  // 屏幕 y 向下 => 顺时针

    subSector_ = -1;
    if (dist_ < std::min(g_.dragThreshold, 1.0)) {
        sector_ = -1;
        escaped_ = false;
        showSub_ = false;
        return;
    }

    const double step = 2 * kPi / n_;
    // 边界归属采用"恰在边界上归逆时针前一扇区"的 ceil 语义（Task 2 裁定）；
    // θ 归一化到 [0,2π) 后仍可能因浮点等于 2π, 连模两次保护。
    int idx = static_cast<int>(std::ceil(angle_ / step - 0.5)) % n_;
    sector_ = (idx + n_) % n_;

    // 子环状态机（M3b）：进入=有子动作且越过主环；迟滞收起=回到 0.9*wheelRadius；
    // 当前扇区无子动作立即关（跨扇区移动不留残影）。
    const int m = parentSubCount(sector_);
    if (m > 0 && dist_ >= a_.wheelRadius) {
        showSub_ = true;
    } else if (m == 0 || dist_ < hysteresisOff_) {
        showSub_ = false;
    }

    // 逃逸判定依赖 showSub_：子环激活时外甩半径放大到 subOuter+20
    const double escapeEff =
        showSub_ ? std::max(escapeDist_, subOuter_ + 20.0) : escapeDist_;
    if (dist_ >= escapeEff) {
        escaped_ = true;
        sector_ = -1;
        subSector_ = -1;
        showSub_ = false;
        return;
    }
    escaped_ = false;
    if (dist_ < coreEff_) {  // 死区: 回到核心圆内, 无选中
        sector_ = -1;
        return;
    }

    // 子槽命中：父扇区角度区间按 m 均分，floor 取整（子区间无共享边界歧义）
    if (showSub_ && m > 0 && dist_ >= a_.wheelRadius && dist_ < subOuter_) {
        const double lo = sector_ * step - step / 2.0;
        double rel = std::fmod(angle_ - lo, 2 * kPi);
        if (rel < 0) rel += 2 * kPi;
        subSector_ = std::clamp(static_cast<int>(std::floor(rel / (step / m))), 0, m - 1);
    }
}

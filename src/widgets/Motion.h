#pragma once

#include <windows.h>

#include <cmath>

namespace tf::widget::motion {

// 悬停 / 按下这套手感参数的唯一出处：1×1 图标和文件夹里的格子共用，
// 以后调手感只改这里，两处一起变。
inline constexpr float kHoverGrow = 0.10f;    // 悬停时图标放大 10%（"略微放大"）
inline constexpr float kPressShrink = 0.12f;  // 按下时缩小 12%
inline constexpr float kHoverTauIn = 0.055f;  // 悬停"进"的时间常数（秒）
inline constexpr float kHoverTauOut = 0.10f;  // 悬停"出"的时间常数，比进慢一点
inline constexpr float kPressStiffness = 1200.0f;
// 临界阻尼是 2*sqrt(k)≈69；取 29（ζ≈0.42）松手才会弹过头一点点
inline constexpr float kPressDamping = 29.0f;

// 指数趋近：和 dt 大小无关（帧率变化时速度不变），永远不越过目标。
inline float Approach(float cur, float target, float dt, float tau) {
    if (tau <= 0.0f) return target;
    const float k = 1.0f - std::exp(-dt / tau);
    return cur + (target - cur) * k;
}

// 欠阻尼弹簧：按下去"沉"一下，松手弹回来并轻微过冲。
// 返回 true 表示还在动（调用方据此决定要不要继续要动画帧）。
inline bool SpringStep(float& value, float& vel, float target, float dt, float stiffness,
                       float damping) {
    const float accel = stiffness * (target - value) - damping * vel;
    vel += accel * dt;
    value += vel * dt;
    if (std::fabs(target - value) < 0.0015f && std::fabs(vel) < 0.02f) {
        value = target;
        vel = 0.0f;
        return false;
    }
    return true;
}

// 图标最终缩放系数：悬停放大 + 按下缩小叠在一起。
// press 在松手后会短暂变负（弹簧过冲），于是图标会"弹大"一点点再落回原位。
inline float IconScale(float hover, float press) {
    return 1.0f + kHoverGrow * hover - kPressShrink * press;
}

}  // namespace tf::widget::motion

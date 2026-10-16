// ANIM-2 A11.1/A12.4 — the pure cross-clip transition state, moved verbatim out
// of transition.hpp. Same smoothstep, shortest-path yaw lerp and advance order.

#include "football/animation/transition.hpp"

#include "football/foundation/math/scalar.hpp"

namespace football::animation {

namespace {

float SmoothStep(float t) {
  const float c = football::math::clamp(t, 0.0f, 1.0f);
  return c * c * (3.0f - 2.0f * c);
}

// Shortest-path angle interpolation, so 0 -> 350 deg blends through 10 deg.
float LerpAngleShortest(float from, float to, float t) {
  const float diff = static_cast<float>(
      football::math::ModulateIntoRange(-football::math::pi, football::math::pi,
                                        football::math::radian(to - from)));
  return from + diff * t;
}

}  // namespace

void AnimationTransitionState::Reset() {
  active = false;
  elapsed_ticks = 0;
  duration_ticks = 0;
  source_pose = PoseFrame{};
  source_base_angle = 0.0f;
  blended_base_angle = 0.0f;
}

void AnimationTransitionState::Begin(const PoseFrame& source,
                                     float source_base_angle_,
                                     int duration_ticks_) {
  source_pose = source;
  source_base_angle = source_base_angle_;
  blended_base_angle = source_base_angle_;
  duration_ticks = duration_ticks_ > 0 ? duration_ticks_ : 0;
  elapsed_ticks = 0;
  active = duration_ticks > 0;
}

PoseFrame AnimationTransitionState::Advance(const PoseFrame& target,
                                            float target_base_angle) {
  if (!active || duration_ticks <= 0) {
    blended_base_angle = target_base_angle;
    return target;
  }
  ++elapsed_ticks;
  if (elapsed_ticks >= duration_ticks) {
    active = false;
    blended_base_angle = target_base_angle;
    return target;
  }
  const float t = static_cast<float>(elapsed_ticks) /
                  static_cast<float>(duration_ticks);
  const float w = SmoothStep(t);
  blended_base_angle = LerpAngleShortest(source_base_angle, target_base_angle, w);
  return BlendPose(source_pose, target, w);
}

}  // namespace football::animation

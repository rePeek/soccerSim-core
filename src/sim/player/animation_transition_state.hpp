#ifndef FOOTBALL_SIM_PLAYER_ANIMATION_TRANSITION_STATE_HPP
#define FOOTBALL_SIM_PLAYER_ANIMATION_TRANSITION_STATE_HPP

#include "football/animation/pose_blend.hpp"
#include "football/foundation/math/scalar.hpp"

// LOCO-2.6b — tick-driven animation transition state.
//
// Pure value state, no Humanoid/Simulation/Ball/RNG. It keeps the current
// animation's integer frame clock (`currentAnim.frameNum`) untouched: only the
// blend between two poses is advanced here, one tick at a time. A transition
// always starts from the *previous tick's final blended pose*, so a mid-blend
// interruption starts from the real output pose, never from the original clip's
// first frame.
//
// LOCO-2.6 hardening: the transition also blends the root base yaw (the
// `base_angle` SampleBakedPose applies in world space), so a clip switch that
// changes base orientation does not rotate the world pose instantly.

inline float SmoothStep(float t) {
  const float c = football::math::clamp(t, 0.0f, 1.0f);
  return c * c * (3.0f - 2.0f * c);
}

// Shortest-path angle interpolation, so 0 -> 350 deg blends through 10 deg.
inline float LerpAngleShortest(float from, float to, float t) {
  const float diff = static_cast<float>(
      football::math::ModulateIntoRange(-football::math::pi, football::math::pi,
                                        football::math::radian(to - from)));
  return from + diff * t;
}

// Fixed, bounded transition length used as the first calibration baseline.
inline constexpr int kDefaultMovementBlendTicks = 8;
// A switch involving a non-Movement action uses a shorter window so the action
// timing (touch frame) is barely disturbed while the pose stays continuous.
inline constexpr int kActionBlendTicks = 4;

inline int BlendTicksForSwitch(bool pure_movement) {
  return pure_movement ? kDefaultMovementBlendTicks : kActionBlendTicks;
}

struct AnimationTransitionState {
  PoseFrame source_pose;
  float source_base_angle = 0.0f;
  float blended_base_angle = 0.0f;  // current output base yaw
  int elapsed_ticks = 0;
  int duration_ticks = 0;
  bool active = false;

  void Reset() {
    active = false;
    elapsed_ticks = 0;
    duration_ticks = 0;
    source_pose = PoseFrame{};
    source_base_angle = 0.0f;
    blended_base_angle = 0.0f;
  }

  // Start a transition from the given pose and base yaw (the last final output).
  void Begin(const PoseFrame& source, float source_base_angle_,
             int duration_ticks_) {
    source_pose = source;
    source_base_angle = source_base_angle_;
    blended_base_angle = source_base_angle_;
    duration_ticks = duration_ticks_ > 0 ? duration_ticks_ : 0;
    elapsed_ticks = 0;
    active = duration_ticks > 0;
  }

  // Advance one tick and return the final local pose. `blended_base_angle` is
  // updated to the matching world base yaw. When inactive it returns the target
  // unchanged and adopts the target base yaw.
  PoseFrame Advance(const PoseFrame& target, float target_base_angle) {
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
};

#endif  // FOOTBALL_SIM_PLAYER_ANIMATION_TRANSITION_STATE_HPP

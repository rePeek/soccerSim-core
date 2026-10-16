#ifndef FOOTBALL_ANIMATION_TRANSITION_HPP
#define FOOTBALL_ANIMATION_TRANSITION_HPP
#include "football/animation/clip.hpp"
#include "football/foundation/math/scalar.hpp"

// ANIM-2 A11.1: moved out of sim/player. Free of Player/Simulation/Ball/RNG;
// the Player keeps the instance and the authoritative action clock.
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
//
// ANIM-2 A12.4: the advancing bodies live in src/animation/transition.cpp.

namespace football::animation {

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

  void Reset();

  // Start a transition from the given pose and base yaw (the last final output).
  void Begin(const PoseFrame& source, float source_base_angle_,
             int duration_ticks_);

  // Advance one tick and return the final local pose. `blended_base_angle` is
  // updated to the matching world base yaw. When inactive it returns the target
  // unchanged and adopts the target base yaw.
  PoseFrame Advance(const PoseFrame& target, float target_base_angle);
};

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TRANSITION_HPP

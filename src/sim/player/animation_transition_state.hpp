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

inline float SmoothStep(float t) {
  const float c = football::math::clamp(t, 0.0f, 1.0f);
  return c * c * (3.0f - 2.0f * c);
}

// Fixed, bounded transition length used as the first calibration baseline.
inline constexpr int kDefaultMovementBlendTicks = 8;

struct AnimationTransitionState {
  PoseFrame source_pose;
  int elapsed_ticks = 0;
  int duration_ticks = 0;
  bool active = false;

  void Reset() {
    active = false;
    elapsed_ticks = 0;
    duration_ticks = 0;
    source_pose = PoseFrame{};
  }

  // Start a transition from the given pose (the last final output pose).
  void Begin(const PoseFrame& source, int duration_ticks_) {
    source_pose = source;
    duration_ticks = duration_ticks_ > 0 ? duration_ticks_ : 0;
    elapsed_ticks = 0;
    active = duration_ticks > 0;
  }

  // Advance one tick and return the final pose for this tick. When inactive it
  // returns the target unchanged (no blend), so the hot path is a plain copy.
  PoseFrame Advance(const PoseFrame& target) {
    if (!active || duration_ticks <= 0) return target;
    ++elapsed_ticks;
    if (elapsed_ticks >= duration_ticks) {
      active = false;
      return target;
    }
    const float t = static_cast<float>(elapsed_ticks) /
                    static_cast<float>(duration_ticks);
    return BlendPose(source_pose, target, SmoothStep(t));
  }
};

#endif  // FOOTBALL_SIM_PLAYER_ANIMATION_TRANSITION_STATE_HPP
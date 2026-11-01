#ifndef FOOTBALL_SIM_PLAYER_ADAPTED_MOTION_HPP
#define FOOTBALL_SIM_PLAYER_ADAPTED_MOTION_HPP

#include <algorithm>
#include <cstdint>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// RMA-0/RMA-2: the unified result of adapting one reference clip to one player.
//
// Root motion and skeleton pose are *one* decision. A sample carries the
// displacement the body intends to make, the pose that performs it, the clip
// time both were evaluated at and the support phase that connects them. No
// later stage may replace one half without re-evaluating the other.
//
// The sample is a value, not an authority: the Player commits it, Runtime
// collision may still constrain it, and a corrected root re-settles the same
// pose. `feasible == false` means the adapter could not produce a valid motion
// (bad clip, unreachable gait) and the caller must fall back or switch action.

struct MotionAdaptationDiagnostics {
  // Reference displacement the clip asked for (world-aligned).
  football::math::Vector3 reference_delta = football::math::Vector3(0);
  // What the capability constraint accepted.
  football::math::Vector3 adapted_delta = football::math::Vector3(0);
  // What the caller finally committed (collision stage).
  football::math::Vector3 final_delta = football::math::Vector3(0);

  float playback_rate = 1.0f;
  float stride_scale = 1.0f;

  float left_support_slip = 0.0f;
  float right_support_slip = 0.0f;
  float max_joint_rotation_step = 0.0f;
  // RM-3: the fastest the reference gait may play under the bounded playback
  // rate. A body above it must brake into range before the root can take over;
  // the compatibility fallback uses this bound instead of the raw control.
  float playable_speed = 0.0f;

  bool capability_limited = false;
  bool collision_limited = false;
  bool fallback_used = false;
  // The style asked for a longer step than the body can make.
  bool reach_limited = false;
  // RMA-7: the clip's source root track already agrees with its stance foot.
  bool ik_free_pose = false;
  // RMA-7: the stance-foot speed is a trustworthy cadence reference, so the
  // Player can calibrate the root in memory even though the authored root
  // track itself is not strict enough for `ik_free_pose`.
  bool calibrated_ik_free = false;
};

struct AdaptedMotionSample {
  AnimationId clip_id = -1;
  // The active displacement this tick. Player position += root_delta.
  football::math::Vector3 root_delta = football::math::Vector3(0);
  // Velocity the displacement implies (root_delta / dt).
  football::math::Vector3 velocity = football::math::Vector3(0);

  // The reference pose sampled at `clip_time`, in the clip's local frame.
  // The Player's warp/realize/settle stages remain the final authority; this is
  // the pose those stages were selected for, not a second skeleton.
  PoseFrame pose;
  float base_angle = 0.0f;

  // Wrapped clip time used for the pose this tick.
  float clip_time = 0.0f;
  float gait_phase = 0.0f;
  float playback_rate = 1.0f;
  float stride_scale = 1.0f;
  bool looped = false;
  // The clip's own local reference displacement over the same interval; the
  // pose warp compares it (rotated by base yaw) with the achieved motion.
  football::math::Vector3 reference_delta_local = football::math::Vector3(0);
  FootSupportFrame support;
  // RM-2: unwrapped clip time the next step starts from. `clip_time` is the
  // wrapped time used for the pose, so a loop seam must not lose the cycle.
  float continuous_clip_time = 0.0f;
  // RM-3: why this sample is (not) executable. `feasible` is kept as the
  // boolean shorthand used by the existing callers.
  enum class Status : std::uint8_t {
    Accepted,
    NeedAnotherClip,
    CapabilityLimited,
    InvalidReference,
  };
  Status status = Status::Accepted;
  bool feasible = true;
  MotionAdaptationDiagnostics diagnostics;
};

// RMA-2/§7: the support-foot slip the final pose shipped, measured between two
// samples of the same gait. Only a foot supported on both sides of the interval
// counts: a foot that is being planted or released really does move.
inline float MeasureSupportSlip(bool previous_support, bool current_support,
                                const football::math::Vector3& previous_ankle,
                                const football::math::Vector3& current_ankle) {
  if (!previous_support || !current_support) return 0.0f;
  return (current_ankle - previous_ankle).Get2D().GetLength();
}

#endif  // FOOTBALL_SIM_PLAYER_ADAPTED_MOTION_HPP

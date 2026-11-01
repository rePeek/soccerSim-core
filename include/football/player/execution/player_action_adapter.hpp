#ifndef FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP

#include <cstdint>

#include "football/animation/clip.hpp"
#include "football/player/execution/player_adapted_motion.hpp"
#include "football/player/execution/player_motion_adapter.hpp"
#include "football/player/execution/player_motion_profile.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// RMA-2: the single pure adapter between a reference clip and one player.
//
// It answers one question, once per tick:
//
//   given this clip, this body, this capability, this control and this playback
//   state, what root displacement *and* which pose can the player actually
//   produce?
//
// The answer is one AdaptedMotionSample. Root timing (clip time, playback rate,
// stride scale) and the displacement are solved together, so a later pose stage
// cannot be handed a root it was not sampled for.
//
// Properties required by the plan:
//   - pure: no Player, no Runtime, no RNG, no event publication;
//   - deterministic: identical inputs produce identical samples;
//   - symmetric to failure: when the clip or the gait cannot satisfy the
//     constraints the sample is marked infeasible instead of stretched.

// A player's stride/cadence preference, as the adapter needs it. The values are
// the neutral shipped gait by default; `min/max` bound the step length the
// caller is willing to author.
struct PlayerGaitRequest {
  float stride_preference = 1.0f;
  float cadence_preference = 1.0f;
  float stride_response = 0.5f;
  float min_stride_scale = 0.6f;
  float max_stride_scale = 1.35f;
};

struct PlayerActionControl {
  // Requested velocity before capability and the animation gait apply.
  football::math::Vector3 desired_velocity = football::math::Vector3(0);
  // Facing used when the desired velocity is zero.
  football::math::Vector3 idle_facing = football::math::Vector3(0);
  // How the reference displacement is interpreted. Ordinary locomotion uses the
  // control target; one-shot actions follow their reference root.
  PlayerMotionAuthority authority = PlayerMotionAuthority::Locomotion;
  // World yaw of the clip's forward (-Y) axis.
  float base_angle = 0.0f;
  PlayerGaitRequest gait;
};

struct PlayerPlaybackState {
  // Continuous clip time at the start of the tick.
  float frame_time = 0.0f;
  // Current smoothed stride scale (carried across ticks).
  float stride_scale = 1.0f;
  // Clip-entry time used when the clip changes underneath the adapter.
  float entry_frame_time = 0.0f;
};

class PlayerActionAdapter final {
 public:
  // Pure and deterministic. Requires `dt > 0` and a clip with at least one
  // pose. A clip that cannot drive a gait returns `feasible == false`; the
  // caller keeps the compatibility path for that tick.
  static AdaptedMotionSample Evaluate(
      const AnimationClip& clip, const PlayerKinematicState& current,
      const PlayerMotionProfile& capability,
      const football::player::PlayerBodyProfile& body,
      const PlayerActionControl& control,
      const PlayerPlaybackState& playback, float dt);
};

#endif  // FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP

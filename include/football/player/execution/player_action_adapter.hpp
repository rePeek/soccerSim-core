#ifndef FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP

#include <cstdint>

#include "football/animation/clip.hpp"
#include "football/foundation/time/tick.hpp"
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

// The adapter is cadence-only in the IK-free runtime path: per-player stride
// preferences belong to pre-baked movement variants, not to a runtime leg
// solve. Keeping them out of this input prevents a config field from looking
// effective when the runtime cannot honour it.
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
  // RMA-5: an explicit world-aligned reference displacement. One-shot actions
  // pass the trajectory their plan produced (including smuggle corrections);
  football::math::Vector3 reference_delta = football::math::Vector3(0);
  // A zero `reference_delta` is a real value (a planted touch frame, a stop).
  // Only `has_reference_delta == false` means "the plan did not supply one".
  bool has_reference_delta = false;
};

struct PlayerPlaybackState {
  // Continuous clip time at the start of the tick.
  float frame_time = 0.0f;
  // Current smoothed stride scale (carried across ticks).
  float stride_scale = 1.0f;
  // Clip-entry time used when the clip changes underneath the adapter.
  float entry_frame_time = 0.0f;
};

// RMA-4: a constant-control forecast. `state` is the kinematic state the
// horizon ends in; `sample` is the last tick's adaptation (root/pose/timing).
struct PredictedMotion {
  PlayerKinematicState state;
  AdaptedMotionSample sample;
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

  // RMA-4: constant-control prediction over a short horizon. It reuses
  // Evaluate() every tick, so the phase/stride evolution and the root are the
  // same ones execution commits. It never touches a Player or a world.
  static PredictedMotion Predict(
      const AnimationClip& clip, const PlayerKinematicState& initial,
      const PlayerMotionProfile& capability,
      const football::player::PlayerBodyProfile& body,
      const PlayerActionControl& control,
      const PlayerPlaybackState& playback, float dt,
      football::sim::TickSpan horizon);
};

#endif  // FOOTBALL_SIM_PLAYER_ACTION_ADAPTER_HPP

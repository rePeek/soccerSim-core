#ifndef FOOTBALL_SIM_PLAYER_MOTION_ADAPTER_HPP
#define FOOTBALL_SIM_PLAYER_MOTION_ADAPTER_HPP

#include <cstdint>

#include "football/player/execution/player_motion_profile.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"

// P2 foundation: a pure numeric core between reference animation motion and the
// player's committed root motion. It does not know about Animation, Ball,
// Runtime, collision or pose: it only answers "given this tick's reference
// displacement and this player's capability, what root displacement is
// physically admissible?".
//
// The caller owns selection, collision and commit. `achieved_delta` is the only
// world displacement result; a later collision/pose stage may constrain it
// further and must feed that final value to the pose adapter.
enum class PlayerMotionAuthority : std::uint8_t {
  // Ordinary running/walking: the control target is the trajectory intent and
  // the reference clip supplies stride/cadence.
  Locomotion,
  // One-shot actions (slide, jump, fall, pass/shot): the reference root is the
  // primary trajectory; capability only bounds it.
  ReferenceAction,
  // Passive collision reaction: physics owns the trajectory; the reference is
  // only a pose source.
  PhysicalReaction,
};

struct PlayerMotionConstraintInput {
  // World-aligned reference displacement for this tick (not a local clip delta).
  football::math::Vector3 reference_delta = football::math::Vector3(0);
  PlayerKinematicState current;
  football::player::PlayerBodyProfile body;
  PlayerMotionProfile capability;
  // Control intent. For Locomotion it is the target velocity; for
  // ReferenceAction it only caps the reference speed when non-zero.
  football::math::Vector3 desired_velocity = football::math::Vector3(0);
  PlayerMotionAuthority authority = PlayerMotionAuthority::ReferenceAction;
  float dt = 0.01f;
};

struct PlayerMotionConstraintResult {
  PlayerKinematicState state;
  football::math::Vector3 achieved_delta = football::math::Vector3(0);
  bool constrained = false;
};

class PlayerMotionAdapter final {
 public:
  // Pure and deterministic. Requires dt > 0 and capability.max_speed > 0.
  static PlayerMotionConstraintResult Step(
      const PlayerMotionConstraintInput& input);
};

#endif  // FOOTBALL_SIM_PLAYER_MOTION_ADAPTER_HPP

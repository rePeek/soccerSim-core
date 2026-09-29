#ifndef _HPP_CORE_MODEL_PLAYER
#define _HPP_CORE_MODEL_PLAYER

#include "../../../defines.hpp"
#include "foundation/math/vector3.hpp"
#include "player_profile.hpp"

// Authoritative, behavior-free movement state of a player (Phase 5).
// Pure simulation data: no Humanoid/Player/controller pointers.
//
// Authority flow (Phases 7F / 7G-0):
//  - Per-tick self-movement and reset/lifecycle evaluations produce a
//    PlayerKinematicResult, applied through PlayerBase::ApplyKinematicResult.
//  - HumanoidBase projects PlayerState into legacy SpatialState; animation root
//    motion is a legacy kinematic evaluator, not self-movement authority.
//  - Collision-driven correction now writes PlayerState first and projects
//    forward into SpatialState (7G-5); Humanoid no longer reverse-syncs it.
//  - The serialized compatibility copy and the bit-exact
//    CheckSimulationKinematicOracle() remain in place.
//
// `speed` is deliberately NOT stored — it is velocity.GetLength() everywhere.
// Use Speed() when the scalar is needed.
//
// Lives next to the player domain entity: player is profile + state + entity.
struct PlayerState {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 movementFacing = blunted::Vector3(0, -1, 0);
  // Torso orientation: procedural PlayerBodyFacing or legacy animation.
  blunted::Vector3 torsoFacing = blunted::Vector3(0, -1, 0);

  // Mirrors position and velocity like the legacy spatial state.
  // movementFacing and torsoFacing remain unchanged: HumanoidBase's spatial
  // state mirror only negates position and movements.
  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(velocity);
    state->process(movementFacing);
    state->process(torsoFacing);
  }
};

// Complete evaluated kinematics for one player tick. This is deliberately
// separate from PlayerState: evaluators produce a result, while PlayerBase
// applies it to the authoritative state owned by World for team slots.
struct PlayerKinematicResult {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 movementFacing = blunted::Vector3(0, -1, 0);
  blunted::Vector3 torsoFacing = blunted::Vector3(0, -1, 0);
};

// Derived accessor: speed is always the length of the velocity vector.
inline float Speed(const PlayerState &state) {
  return state.velocity.GetLength();
}

namespace football::model {

// Player is the football domain entity (Phase 7F-0).
//
// It binds a match-lifetime profile to the World-owned state without owning
// movement algorithms. Animation, AI and action compatibility stay in the
// legacy Player/PlayerBase facade.
class Player {
 public:
  Player(const PlayerProfile& profile, PlayerState& state);
  const PlayerProfile& Profile() const { return profile_; }

  PlayerState& State() { return state_; }
  const PlayerState& State() const { return state_; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  const blunted::Vector3& MovementFacing() const { return state_.movementFacing; }
  const blunted::Vector3& TorsoFacing() const { return state_.torsoFacing; }

 private:
  const PlayerProfile& profile_;
  PlayerState& state_;
};

}  // namespace football::model

#endif  // _HPP_CORE_MODEL_PLAYER
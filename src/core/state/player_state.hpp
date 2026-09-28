#ifndef _HPP_CORE_STATE_PLAYER_STATE
#define _HPP_CORE_STATE_PLAYER_STATE

#include "../../defines.hpp"
#include "foundation/math/vector3.hpp"

// Authoritative, behavior-free movement state of a player (Phase 5).
// Pure simulation data: no Humanoid/Player/controller pointers.
//
// Authority flow (Phases 7F / 7G-0):
//  - Per-tick self-movement and reset/lifecycle evaluations produce a
//    PlayerKinematicResult, applied through PlayerBase::ApplyKinematicResult.
//  - HumanoidBase projects PlayerState into legacy SpatialState; animation root
//    motion is a legacy kinematic evaluator, not self-movement authority.
//  - Collision-driven correction remains a transitional reverse sync for 7G.
//  - The serialized compatibility copy and the bit-exact
//    CheckSimulationKinematicOracle() remain in place.
//
// `speed` is deliberately NOT stored — it is velocity.GetLength() everywhere.
// Use Speed() when the scalar is needed.
struct PlayerState {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(0, -1, 0);
  // facing is locomotion direction; bodyFacing is torso orientation, produced
  // by PlayerBodyFacing on procedural ticks and by animation otherwise.
  blunted::Vector3 bodyFacing = blunted::Vector3(0, -1, 0);

  // Mirrors position and velocity like the legacy spatial state. Facing and
  // bodyFacing are deliberately left untouched: HumanoidBase's spatial state
  // mirror only negates position and movements.
  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(velocity);
    state->process(facing);
    state->process(bodyFacing);
  }
};

// Complete evaluated kinematics for one player tick. This is deliberately
// separate from PlayerState: evaluators produce a result, while PlayerBase
// applies it to the authoritative state owned by World for team slots.
struct PlayerKinematicResult {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(0, -1, 0);
  blunted::Vector3 bodyFacing = blunted::Vector3(0, -1, 0);
};

// Derived accessor: speed is always the length of the velocity vector.
inline float Speed(const PlayerState &state) {
  return state.velocity.GetLength();
}

#endif  // _HPP_CORE_STATE_PLAYER_STATE
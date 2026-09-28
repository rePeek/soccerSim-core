#ifndef _HPP_CORE_STATE_PLAYER_STATE
#define _HPP_CORE_STATE_PLAYER_STATE

#include "../../defines.hpp"
#include "foundation/math/vector3.hpp"

// Authoritative, behavior-free movement state of a player (Phase 5).
// Pure simulation data: no Humanoid/Player/controller pointers.
//
// `speed` is deliberately NOT stored — it is velocity.GetLength() everywhere.
// Use Speed() when the scalar is needed.
struct PlayerState {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(0, -1, 0);
  // Separate from facing: facing is the locomotion direction, bodyFacing is
  // the animation-derived body pose.
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

// Derived accessor: speed is always the length of the velocity vector.
inline float Speed(const PlayerState &state) {
  return state.velocity.GetLength();
}

#endif  // _HPP_CORE_STATE_PLAYER_STATE
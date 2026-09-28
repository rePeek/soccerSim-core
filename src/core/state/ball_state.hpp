#ifndef _HPP_CORE_STATE_BALL_STATE
#define _HPP_CORE_STATE_BALL_STATE

#include "foundation/math/quaternion.hpp"
#include "../../defines.hpp"
#include "foundation/math/vector3.hpp"

// Authoritative, behavior-free state of the ball at one simulation tick.
//
// Plain simulation data only: no pointers to Match/GameEnv/controllers, no
// prediction caches. Those stay in legacy Ball (Phase 4).
struct BallState {
  blunted::Vector3 position;
  blunted::Vector3 momentum;        // meters / sec
  blunted::Quaternion rotation_ms;  // radians per second per axis
  blunted::Quaternion orientation;  // accumulated orientation

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(momentum);
    state->process(rotation_ms);
    state->process(orientation);
  }
};

#endif  // _HPP_CORE_STATE_BALL_STATE
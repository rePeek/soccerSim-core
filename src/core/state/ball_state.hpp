#ifndef _HPP_CORE_STATE_BALL_STATE
#define _HPP_CORE_STATE_BALL_STATE

#include "foundation/math/quaternion.hpp"
#include "../../defines.hpp"
#include "foundation/math/vector3.hpp"

// Authoritative, behavior-free state of the ball at one simulation tick.
//
// Plain simulation data only: no pointers to Match/GameEnv/controllers, no
// prediction caches. Those stay in legacy Ball (Phase 4).
//
// 7G-7b field rename: the old `momentum` was really a velocity (m/s) and the
// old `rotation_ms` quaternion was really an angular velocity (rad/s per
// axis). Both now carry the names and types that match their meaning.
struct BallState {
  blunted::Vector3 position;
  blunted::Vector3 velocity;        // meters per second
  blunted::Vector3 angularVelocity; // radians per second, per axis
  blunted::Quaternion orientation;  // accumulated orientation

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(velocity);
    state->process(angularVelocity);
    state->process(orientation);
  }
};

#endif  // _HPP_CORE_STATE_BALL_STATE
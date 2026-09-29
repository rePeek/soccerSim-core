#ifndef _HPP_CORE_MODEL_BALL
#define _HPP_CORE_MODEL_BALL

#include "foundation/math/quaternion.hpp"
#include "../../../defines.hpp"
#include "foundation/math/vector3.hpp"
#include "ball_profile.hpp"

// Authoritative, behavior-free state of the ball at one simulation tick.
//
// Plain simulation data only: no pointers to Match/GameEnv/controllers, no
// prediction caches. Those stay in legacy Ball (Phase 4).
//
// 7G-7b field rename: the old `momentum` was really a velocity (m/s) and the
// old `rotation_ms` quaternion was really an angular velocity (rad/s per
// axis). Both now carry the names and types that match their meaning.
//
// The state lives here, next to the domain entity, because the ball domain is
// exactly profile + state + entity (WorldState owns the state; Ball borrows).
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

namespace football::model {

// Ball is the football domain entity (Phase 7E.5).
//
// It references the authoritative BallState (owned by WorldState) and exposes
// the ball's domain operations. Prediction caches, mental-image and
// possession updates, and the Match pointer stay in the legacy BallLegacy
// facade, which composes this entity.
class Ball {
 public:
  Ball(const BallProfile& profile, BallState& state);
  const BallProfile& Profile() const { return profile_; }

  BallState& State() { return state_; }
  const BallState& State() const { return state_; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  float Speed() const { return state_.velocity.GetLength(); }

  void SetPosition(const blunted::Vector3& position) {
    state_.position = position;
  }
  void SetVelocity(const blunted::Vector3& velocity) {
    state_.velocity = velocity;
  }
  void SetRotation(blunted::real x, blunted::real y, blunted::real z,
                   float bias = 1.0f);
  void Reset(const blunted::Vector3& focusPos);

 private:
  const BallProfile& profile_;
  BallState& state_;
};

}  // namespace football::model

#endif  // _HPP_CORE_MODEL_BALL
#ifndef _HPP_CORE_DOMAIN_BALL
#define _HPP_CORE_DOMAIN_BALL

#include "core/state/ball_state.hpp"

// Ball is the football domain entity (Phase 7E.5).
//
// It references the authoritative BallState (owned by WorldState) and exposes
// the ball's domain operations. Prediction caches, mental-image and
// possession updates, and the Match pointer stay in the legacy BallLegacy
// facade, which composes this entity.
class Ball {
public:
  explicit Ball(BallState& state);

  BallState& State() { return state_; }
  const BallState& State() const { return state_; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Momentum() const { return state_.momentum; }
  float Speed() const { return state_.momentum.GetLength(); }

  void SetPosition(const blunted::Vector3& position) {
    state_.position = position;
  }
  void SetMomentum(const blunted::Vector3& momentum) {
    state_.momentum = momentum;
  }
  void SetRotation(blunted::real x, blunted::real y, blunted::real z,
                   float bias = 1.0f);
  void Reset(const blunted::Vector3& focusPos);

private:
  BallState& state_;
};

#endif  // _HPP_CORE_DOMAIN_BALL
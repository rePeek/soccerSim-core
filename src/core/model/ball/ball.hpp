#ifndef FOOTBALL_CORE_MODEL_BALL_HPP
#define FOOTBALL_CORE_MODEL_BALL_HPP

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

namespace football::model {

// Dynamic simulation data for one ball. It deliberately lives beside Ball,
// rather than in a separate state module: a Ball is immutable configuration
// plus this mutable value.
struct BallState {
  blunted::Vector3 position = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 velocity = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Vector3 angularVelocity = blunted::Vector3(0.0f, 0.0f, 0.0f);
  blunted::Quaternion orientation =
      blunted::Quaternion(blunted::QUATERNION_IDENTITY);
};

// Thin simulation model. Fixed physical attributes are set once at
// construction and cannot change afterwards. Physics computes a complete next
// BallState and commits it through SetState(); the model owns no algorithms.
class Ball {
 public:
  Ball(float radius = 0.11f, float mass = 0.43f,
       float inertiaFactor = 2.0f / 3.0f);

  float Radius() const { return radius_; }
  float Mass() const { return mass_; }
  float InertiaFactor() const { return inertiaFactor_; }

  const BallState& State() const { return state_; }
  void SetState(const BallState& next) { state_ = next; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  float Speed() const { return state_.velocity.GetLength(); }

  void Reset(const blunted::Vector3& focusPosition);

 private:
  const float radius_;
  const float mass_;
  const float inertiaFactor_;
  BallState state_;
};

}  // namespace football::model

#endif  // FOOTBALL_CORE_MODEL_BALL_HPP

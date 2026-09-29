#ifndef FOOTBALL_CORE_MODEL_BALL_HPP
#define FOOTBALL_CORE_MODEL_BALL_HPP

#include "core/math/quaternion.hpp"
#include "core/math/vector3.hpp"

namespace football_sim {

// Dynamic simulation data for one ball. It deliberately lives beside Ball,
// rather than in a separate state module: a Ball is immutable configuration
// plus this mutable value.
struct BallState {
  football_sim::math::Vector3 position = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 velocity = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Vector3 angularVelocity = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  football_sim::math::Quaternion orientation =
      football_sim::math::Quaternion(football_sim::math::QUATERNION_IDENTITY);
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

  const football_sim::math::Vector3& Position() const { return state_.position; }
  const football_sim::math::Vector3& Velocity() const { return state_.velocity; }
  float Speed() const { return state_.velocity.GetLength(); }

  void Reset(const football_sim::math::Vector3& focusPosition);

 private:
  const float radius_;
  const float mass_;
  const float inertiaFactor_;
  BallState state_;
};

}  // namespace football_sim

#endif  // FOOTBALL_CORE_MODEL_BALL_HPP

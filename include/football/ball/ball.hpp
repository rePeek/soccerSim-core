#pragma once

#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::pitch { class Pitch; }

namespace football::ball {

class BallModel {
 public:
  constexpr BallModel() = default;

  BallModel(float mass, float radius, float magnus_coefficient = 0.02f)
      : mass_(mass),
        radius_(radius),
        magnus_coefficient_(magnus_coefficient) {
    if (!std::isfinite(mass) || !std::isfinite(radius) ||
        !std::isfinite(magnus_coefficient) ||
        mass <= 0.0f || radius <= 0.0f || magnus_coefficient < 0.0f) {
      throw std::invalid_argument("Invalid ball configuration");
    }
  }

  constexpr float mass() const { return mass_; }
  constexpr float radius() const { return radius_; }
  constexpr float magnus_coefficient() const { return magnus_coefficient_; }

  bool operator==(const BallModel&) const = default;

 private:
  float mass_ = 0.43f;
  float radius_ = 0.11f;
  float magnus_coefficient_ = 0.02f;
};

struct BallImpulse {
  football::math::Vector3 impulse = football::math::Vector3(0);
  football::math::Vector3 contact_point = football::math::Vector3(0);
};

struct BallState {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
  football::math::Vector3 angular_velocity;
  football::math::Quaternion orientation;
};

// Defined in ball_contact.hpp together with BallContact and AdvanceBallTick.
struct BallStepResult;

struct BallTickInput {
  std::span<const football::geometry::ColliderMotion> dynamic_colliders;
  std::optional<BallImpulse> active_impulse;
};

class Ball {
 public:
  Ball(const football::ball::BallModel& model,
       const football::pitch::Pitch& pitch);
  ~Ball();

  BallState state() const;
  const football::ball::BallModel& config() const { return config_; }

  BallStepResult Step(const BallTickInput& input);
  BallState Predict(football::sim::TickSpan ahead) const;
  BallStepResult Predict(const BallTickInput& input) const;

  void Reset(const BallState& state);

  // Authoritative ground placement: z is forced to the ball radius and
  // velocity/spin are cleared. Airborne states still use Reset(BallState).
  void PlaceOnGround(float x, float y);

 private:
  BallStepResult AdvanceState(
      const BallState& current,
      std::span<const football::geometry::ColliderMotion> colliders) const;
  BallStepResult EvaluateTick(const BallTickInput& input,
                              std::vector<football::geometry::ColliderMotion>& scratch) const;

  football::ball::BallModel config_;
  const football::pitch::Pitch& pitch_;
  BallState state_;
  std::vector<football::geometry::ColliderMotion> tick_colliders_;
};

}  // namespace football::ball

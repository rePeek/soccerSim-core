#ifndef FOOTBALL_BALL_BALL_MODEL_HPP
#define FOOTBALL_BALL_BALL_MODEL_HPP

#include <cmath>
#include <stdexcept>

namespace football::ball {

// Static physical description of the football itself: mass, radius and the
// aerodynamic coefficients. It owns no runtime state and is validated at
// construction.
//
// Gravity, wind, the ground, the goal and every static collider are properties
// of the world, so they live in football::pitch::Pitch, not here.
class BallModel {
 public:
  constexpr BallModel() = default;

  BallModel(float mass, float radius, float drag = 0.015f,
            float magnus_coefficient = 0.02f)
      : mass_(mass),
        radius_(radius),
        drag_(drag),
        magnus_coefficient_(magnus_coefficient) {
    if (!std::isfinite(mass) || !std::isfinite(radius) ||
        !std::isfinite(drag) || !std::isfinite(magnus_coefficient) ||
        mass <= 0.0f || radius <= 0.0f ||
        drag < 0.0f || magnus_coefficient < 0.0f) {
      throw std::invalid_argument("Invalid ball configuration");
    }
  }

  constexpr float mass() const { return mass_; }
  constexpr float radius() const { return radius_; }
  // Air drag coefficient: acceleration is -drag * |v| * v.
  constexpr float drag() const { return drag_; }
  // Magnus coupling: acceleration is magnus * (spin x v).
  constexpr float magnus_coefficient() const { return magnus_coefficient_; }

  bool operator==(const BallModel&) const = default;

 private:
  float mass_ = 0.43f;
  float radius_ = 0.11f;
  float drag_ = 0.015f;
  float magnus_coefficient_ = 0.02f;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_MODEL_HPP

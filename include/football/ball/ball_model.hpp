#pragma once

#include <cmath>
#include <stdexcept>

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

}

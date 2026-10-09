#ifndef FOOTBALL_MODEL_BALL_CONFIG_HPP
#define FOOTBALL_MODEL_BALL_CONFIG_HPP

#include <cmath>
#include <stdexcept>

namespace football::model {

// Static physical description of the football. It owns no runtime state and is
// validated at construction; consumers read it through getters only.
//
// `restitution` currently represents the effective ball-ground contact in the
// legacy physics and is not a pure ball material property. Keep it here to
// preserve behaviour until a contact-material model is introduced.
class BallConfig {
 public:
  constexpr BallConfig() = default;

  BallConfig(float mass, float radius, float restitution = 0.62f,
             float drag = 0.015f)
      : mass_(mass),
        radius_(radius),
        restitution_(restitution),
        drag_(drag) {
    if (!std::isfinite(mass) || !std::isfinite(radius) ||
        !std::isfinite(restitution) || !std::isfinite(drag) ||
        mass <= 0.0f || radius <= 0.0f ||
        restitution < 0.0f || restitution > 1.0f ||
        drag < 0.0f) {
      throw std::invalid_argument("Invalid ball configuration");
    }
  }

  constexpr float mass() const { return mass_; }
  constexpr float radius() const { return radius_; }
  constexpr float restitution() const { return restitution_; }
  constexpr float drag() const { return drag_; }

  bool operator==(const BallConfig&) const = default;

 private:
  float mass_ = 0.43f;
  float radius_ = 0.11f;
  float restitution_ = 0.62f;
  float drag_ = 0.015f;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_BALL_CONFIG_HPP

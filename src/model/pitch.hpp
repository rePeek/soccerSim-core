#ifndef FOOTBALL_MODEL_PITCH_HPP
#define FOOTBALL_MODEL_PITCH_HPP

#include <cmath>
#include <stdexcept>

namespace football::model {

// Read-only pitch description in metres: x is length, y is width, and the
// centre spot is the origin. Geometry and ground-surface parameters are fixed
// at construction; the match must not mutate them at runtime.
//
// Ground parameters keep the legacy empirical values, not standard Coulomb
// coefficients, so migration to Pitch preserves the historical simulation
// behaviour exactly.
class Pitch {
 public:
  constexpr Pitch() = default;

  // Core geometry and ground physics are constructor-required; remaining
  // dimensions keep their legacy defaults. Invalid (non-finite or
  // non-positive geometry / negative friction) values are rejected here so
  // every consumer shares one validated description.
  Pitch(float length, float width, float quadratic_resistance,
        float ground_deceleration, float grass_height)
      : length_(length),
        width_(width),
        quadratic_resistance_(quadratic_resistance),
        ground_deceleration_(ground_deceleration),
        grass_height_(grass_height) {
    if (!std::isfinite(length) || !std::isfinite(width) ||
        !std::isfinite(quadratic_resistance) || !std::isfinite(ground_deceleration) ||
        !std::isfinite(grass_height) ||
        length <= 0.0f || width <= 0.0f ||
        quadratic_resistance < 0.0f || ground_deceleration < 0.0f ||
        grass_height < 0.0f) {
      throw std::invalid_argument(
          "football::model::Pitch: invalid geometry or ground parameters");
    }
  }

  constexpr float length() const { return length_; }
  constexpr float width() const { return width_; }
  constexpr float half_length() const { return length_ * 0.5f; }
  constexpr float half_width() const { return width_ * 0.5f; }
  constexpr float line_half_width() const { return line_width_ * 0.5f; }
  constexpr float goal_half_width() const { return goal_width_ * 0.5f; }
  constexpr float goal_height() const { return goal_height_; }
  constexpr float goal_depth() const { return goal_depth_; }

  constexpr float quadratic_resistance() const { return quadratic_resistance_; }
  constexpr float ground_deceleration() const { return ground_deceleration_; }
  constexpr float grass_height() const { return grass_height_; }
  // Penalty-area geometry used by the referee and restart placement.
  constexpr float penalty_area_depth() const { return penalty_area_depth_; }
  constexpr float penalty_mark_distance() const { return penalty_mark_distance_; }

  // Ground-plane bounds only. Ball radius, line crossing and restart decisions
  // belong to the runtime simulation and referee, not this domain description.
  constexpr bool contains(float x, float y) const {
    return x >= -half_length() && x <= half_length() &&
           y >= -half_width() && y <= half_width();
  }

  bool operator==(const Pitch&) const = default;

 private:
  float length_ = 105.0f;
  float width_ = 68.0f;
  float quadratic_resistance_ = 0.04f;
  float ground_deceleration_ = 1.6f;
  float grass_height_ = 0.025f;
  float line_width_ = 0.12f;
  float goal_width_ = 7.32f;
  float goal_height_ = 2.44f;
  float goal_depth_ = 2.55f;
  float penalty_area_depth_ = 16.5f;
  float penalty_mark_distance_ = 11.0f;
};

constexpr Pitch MakeLegacyPitch() { return Pitch{}; }

}  // namespace football::model

#endif  // FOOTBALL_MODEL_PITCH_HPP

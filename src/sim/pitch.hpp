#ifndef FOOTBALL_SIM_PITCH_HPP
#define FOOTBALL_SIM_PITCH_HPP

#include "foundation/math/vector3.hpp"

// Read-only pitch geometry in simulation metres: x is length, y is width,
// and the centre spot is the origin. This initial version deliberately exposes
// only the legacy geometry; arbitrary dimensions also require migrating the
// remaining AI and environment-coordinate assumptions.
class Pitch {
 public:
  constexpr Pitch() = default;

  constexpr float length() const { return length_; }
  constexpr float width() const { return width_; }
  constexpr float half_length() const { return length_ * 0.5f; }
  constexpr float half_width() const { return width_ * 0.5f; }
  constexpr float full_half_length() const { return full_length_ * 0.5f; }
  constexpr float full_half_width() const { return full_width_ * 0.5f; }
  constexpr float line_half_width() const { return line_width_ * 0.5f; }
  constexpr float goal_half_width() const { return goal_width_ * 0.5f; }
  constexpr float goal_height() const { return goal_height_; }
  constexpr float goal_depth() const { return goal_depth_; }

  // Geometric bounds only. Ball radius, line crossing and restart decisions
  // remain responsibilities of the simulation and referee.
  bool contains(const blunted::Vector3& position) const {
    return position.coords[0] >= -half_length() &&
           position.coords[0] <= half_length() &&
           position.coords[1] >= -half_width() &&
           position.coords[1] <= half_width();
  }

  bool operator==(const Pitch&) const = default;

 private:
  float length_ = 110.0f;
  float width_ = 72.0f;
  float full_length_ = 120.0f;
  float full_width_ = 80.0f;
  float line_width_ = 0.12f;
  float goal_width_ = 7.4f;
  float goal_height_ = 2.5f;
  float goal_depth_ = 2.55f;
};

constexpr Pitch MakeLegacyPitch() { return Pitch{}; }

#endif  // FOOTBALL_SIM_PITCH_HPP

#ifndef FOOTBALL_MODEL_PITCH_HPP
#define FOOTBALL_MODEL_PITCH_HPP

namespace football::model {

// Read-only pitch geometry in metres: x is length, y is width, and the centre
// spot is the origin. This version deliberately exposes only legacy geometry;
// arbitrary dimensions also require migrating AI and coordinate assumptions.
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

  // Ground-plane bounds only. Ball radius, line crossing and restart decisions
  // belong to the runtime simulation and referee, not this domain description.
  constexpr bool contains(float x, float y) const {
    return x >= -half_length() && x <= half_length() &&
           y >= -half_width() && y <= half_width();
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

}  // namespace football::model

#endif  // FOOTBALL_MODEL_PITCH_HPP

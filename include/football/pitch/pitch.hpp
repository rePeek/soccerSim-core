#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "football/foundation/geometry/collider.hpp"
#include "football/models.h"

namespace football::pitch {

class Pitch {
 public:
  explicit Pitch(const PitchModel& model =
      ::football_pitch_model{.length = 105.0f, .width = 68.0f, .quadratic_resistance = 0.04f,
                             .ground_deceleration = 1.6f, .air_drag = 0.015f});
  const PitchModel& model() const { return model_; }

  constexpr float length() const { return model_.length; }
  constexpr float width() const { return model_.width; }
  constexpr float half_length() const { return model_.length * 0.5f; }
  constexpr float half_width() const { return model_.width * 0.5f; }
  constexpr float line_half_width() const { return line_width_ * 0.5f; }
  constexpr float goal_half_width() const { return goal_width_ * 0.5f; }
  constexpr float goal_height() const { return goal_height_; }
  constexpr float goal_depth() const { return goal_depth_; }
  constexpr float penalty_area_depth() const { return penalty_area_depth_; }
  constexpr float penalty_mark_distance() const { return penalty_mark_distance_; }

  constexpr float gravity() const { return gravity_; }
  constexpr float quadratic_resistance() const { return model_.quadratic_resistance; }
  constexpr float ground_deceleration() const { return model_.ground_deceleration; }
  constexpr float air_drag() const { return model_.air_drag; }

  std::span<const geometry::Collider> colliders() const {
    return colliders_;
  }

  constexpr bool contains(float x, float y) const {
    return x >= -half_length() && x <= half_length() &&
           y >= -half_width() && y <= half_width();
  }

  bool operator==(const Pitch& other) const;

 private:
  static constexpr std::size_t kGoalParts = 6;
  static constexpr std::size_t kColliderCount = 1 + kGoalParts;

  void BuildGrass(geometry::Collider& target) const;
  void BuildGoal(std::span<geometry::Collider> target) const;

  PitchModel model_;
  float line_width_ = 0.12f;
  float goal_width_ = 7.32f;
  float goal_height_ = 2.44f;
  float goal_depth_ = 2.55f;
  float penalty_area_depth_ = 16.5f;
  float penalty_mark_distance_ = 11.0f;
  float gravity_ = -9.81f;

  geometry::ContactMaterial ground_material_{0.62f, 0.0f};
  geometry::ContactMaterial post_material_{0.8f, 0.0f};
  std::array<geometry::Collider, kColliderCount> colliders_{};
};

}

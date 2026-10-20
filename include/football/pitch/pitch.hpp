#pragma once

#include <span>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/geometry/collider.hpp"

namespace football::pitch {

class Pitch {
 public:
  Pitch(float length = 105.0f, float width = 68.0f,
        float quadratic_resistance = 0.04f, float ground_deceleration = 1.6f,
        float grass_height = 0.025f);

  constexpr float length() const { return length_; }
  constexpr float width() const { return width_; }
  constexpr float half_length() const { return length_ * 0.5f; }
  constexpr float half_width() const { return width_ * 0.5f; }
  constexpr float line_half_width() const { return line_width_ * 0.5f; }
  constexpr float goal_half_width() const { return goal_width_ * 0.5f; }
  constexpr float goal_height() const { return goal_height_; }
  constexpr float goal_depth() const { return goal_depth_; }
  constexpr float penalty_area_depth() const { return penalty_area_depth_; }
  constexpr float penalty_mark_distance() const { return penalty_mark_distance_; }

  const football::math::Vector3& gravity() const { return gravity_; }
  const football::math::Vector3& wind_velocity() const { return wind_velocity_; }
  constexpr float quadratic_resistance() const { return quadratic_resistance_; }
  constexpr float ground_deceleration() const { return ground_deceleration_; }
  constexpr float grass_height() const { return grass_height_; }

  std::span<const football::geometry::ColliderMotion> colliders() const {
    return colliders_;
  }

  constexpr bool contains(float x, float y) const {
    return x >= -half_length() && x <= half_length() &&
           y >= -half_width() && y <= half_width();
  }

  bool operator==(const Pitch& other) const;

 private:
  void BuildColliders();
  void BuildGrass();
  void BuildGoal();

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

  football::math::Vector3 gravity_{0.0f, 0.0f, -9.81f};
  football::math::Vector3 wind_velocity_{0.0f, 0.0f, 0.0f};
  football::geometry::ContactMaterial ground_material_{0.62f, 0.0f};
  football::geometry::ContactMaterial post_material_{0.8f, 0.0f};
  std::vector<football::geometry::ColliderMotion> colliders_;
};

}

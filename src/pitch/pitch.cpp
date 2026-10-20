#include "football/pitch/pitch.hpp"

#include <cmath>
#include <stdexcept>

namespace football::pitch {

namespace {

constexpr football::geometry::ColliderId kGroundColliderId = 1;
constexpr football::geometry::ColliderId kLeftPostLowColliderId = 2;
constexpr football::geometry::ColliderId kLeftPostHighColliderId = 3;
constexpr football::geometry::ColliderId kRightPostLowColliderId = 4;
constexpr football::geometry::ColliderId kRightPostHighColliderId = 5;
constexpr football::geometry::ColliderId kLeftBarColliderId = 6;
constexpr football::geometry::ColliderId kRightBarColliderId = 7;
constexpr float kPostRadius = 0.06f;

football::geometry::ColliderMotion StaticCollider(
    football::geometry::ColliderId id, football::geometry::ColliderShape shape,
    football::geometry::ContactMaterial material) {
  football::geometry::ColliderMotion motion;
  motion.id = id;
  motion.start = shape;
  motion.end = shape;
  motion.material = material;
  return motion;
}

}

Pitch::Pitch(float length, float width, float quadratic_resistance,
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
      grass_height <= 0.0f) {
    throw std::invalid_argument(
        "football::pitch::Pitch: invalid geometry or ground parameters");
  }
  BuildColliders();
}

bool Pitch::operator==(const Pitch& other) const {
  return length_ == other.length_ && width_ == other.width_ &&
         quadratic_resistance_ == other.quadratic_resistance_ &&
         ground_deceleration_ == other.ground_deceleration_ &&
         grass_height_ == other.grass_height_ && line_width_ == other.line_width_ &&
         goal_width_ == other.goal_width_ && goal_height_ == other.goal_height_ &&
         goal_depth_ == other.goal_depth_ &&
         penalty_area_depth_ == other.penalty_area_depth_ &&
         penalty_mark_distance_ == other.penalty_mark_distance_ &&
         gravity_ == other.gravity_ && wind_velocity_ == other.wind_velocity_ &&
         ground_material_ == other.ground_material_ &&
         post_material_ == other.post_material_;
}

void Pitch::BuildColliders() {
  colliders_.clear();
  colliders_.reserve(7);
  BuildGrass();
  BuildGoal();
}

void Pitch::BuildGrass() {
  using football::geometry::HalfSpace;
  colliders_.push_back(StaticCollider(
      kGroundColliderId,
      HalfSpace{football::math::Vector3(0, 0, 0), football::math::Vector3(0, 0, 1)},
      ground_material_));
}

void Pitch::BuildGoal() {
  using football::geometry::Capsule;
  const float x = half_length();
  const float y = goal_half_width();
  const float z = goal_height();
  colliders_.push_back(StaticCollider(
      kLeftPostLowColliderId,
      Capsule{football::math::Vector3(-x, -y, 0), football::math::Vector3(-x, -y, z), kPostRadius},
      post_material_));
  colliders_.push_back(StaticCollider(
      kLeftPostHighColliderId,
      Capsule{football::math::Vector3(-x, y, 0), football::math::Vector3(-x, y, z), kPostRadius},
      post_material_));
  colliders_.push_back(StaticCollider(
      kRightPostLowColliderId,
      Capsule{football::math::Vector3(x, -y, 0), football::math::Vector3(x, -y, z), kPostRadius},
      post_material_));
  colliders_.push_back(StaticCollider(
      kRightPostHighColliderId,
      Capsule{football::math::Vector3(x, y, 0), football::math::Vector3(x, y, z), kPostRadius},
      post_material_));
  colliders_.push_back(StaticCollider(
      kLeftBarColliderId,
      Capsule{football::math::Vector3(-x, -y, z), football::math::Vector3(-x, y, z), kPostRadius},
      post_material_));
  colliders_.push_back(StaticCollider(
      kRightBarColliderId,
      Capsule{football::math::Vector3(x, -y, z), football::math::Vector3(x, y, z), kPostRadius},
      post_material_));
}

}

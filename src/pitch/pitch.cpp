// football::pitch::Pitch implementation: one-time generation of the static scene.
// Keep collider ids/order, material coefficients and float arithmetic stable.

#include "football/pitch/pitch.hpp"

#include <format>

#include "football/log/log.hpp"

using namespace football::log;

namespace football::pitch {

namespace {

// Stable ids and geometry frozen because the CCD uses ColliderId for
// deterministic contact sorting.
constexpr football::geometry::ColliderId kGroundColliderId = 1;
constexpr football::geometry::ColliderId kLeftPostLowColliderId = 2;
constexpr football::geometry::ColliderId kLeftPostHighColliderId = 3;
constexpr football::geometry::ColliderId kRightPostLowColliderId = 4;
constexpr football::geometry::ColliderId kRightPostHighColliderId = 5;
constexpr football::geometry::ColliderId kLeftBarColliderId = 6;
constexpr football::geometry::ColliderId kRightBarColliderId = 7;
constexpr float kPostRadius = 0.06f;

// Deterministic static collider: same shape at start and end (no motion).
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

}  // namespace

Pitch::Pitch(const PitchModel& model) : model_(model) {
  BuildColliders();
  Info(Channel::pitch, std::format("scene built: {}x{} m, {} colliders", model_.length,
                                  model_.width, colliders_.size()));
}

// Equality compares the configuration only. The collider list is derived data
// rebuilt from the configuration in the constructor, so it is not part of a
// pitch's identity.
bool Pitch::operator==(const Pitch& other) const {
  return model_.length == other.model_.length &&
         model_.width == other.model_.width &&
         model_.quadratic_resistance == other.model_.quadratic_resistance &&
         model_.ground_deceleration == other.model_.ground_deceleration &&
         model_.air_drag == other.model_.air_drag &&
         line_width_ == other.line_width_ &&
         goal_width_ == other.goal_width_ && goal_height_ == other.goal_height_ &&
         goal_depth_ == other.goal_depth_ &&
         penalty_area_depth_ == other.penalty_area_depth_ &&
         penalty_mark_distance_ == other.penalty_mark_distance_ &&
         gravity_ == other.gravity_ &&
         ground_material_ == other.ground_material_ &&
         post_material_ == other.post_material_;
}

// The whole immutable scene, built once at construction.
void Pitch::BuildColliders() {
  colliders_.clear();
  colliders_.reserve(7);
  BuildGrass();
  BuildGoal();
}

// Grass: the ground half-space the ball rolls on. Rolling resistance and grass
// height are the ground_* fields, not properties of the HalfSpace geometry;
// the effective ball-ground restitution is the ground contact material.
void Pitch::BuildGrass() {
  using football::geometry::HalfSpace;
  colliders_.push_back(StaticCollider(
      kGroundColliderId,
      HalfSpace{football::math::Vector3(0, 0, 0), football::math::Vector3(0, 0, 1)},
      ground_material_));
}

// Goal: four posts plus two crossbars, all steel.
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

}  // namespace football::pitch

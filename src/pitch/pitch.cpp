// football::pitch::Pitch implementation: one-time generation of the static scene.
// Keep collider order, names, material coefficients and float arithmetic stable.

#include "football/pitch/pitch.hpp"

#include <format>

#include "football/log/log.hpp"

using namespace football::log;

namespace football::pitch {

Pitch::Pitch(const PitchModel& model) : model_(model) {
  BuildGrass(colliders_.front());
  BuildGoal(std::span(colliders_).subspan(1));

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

// Grass: the ground half-space the ball rolls on. Rolling resistance and grass
// height are the ground_* fields, not properties of the HalfSpace geometry;
// the effective ball-ground restitution is the ground contact material.
void Pitch::BuildGrass(geometry::Collider& target) const {
  using geometry::HalfSpace;
  target = {HalfSpace{math::Vector3(0), math::Vector3(0, 0, 1)},
            ground_material_, "ground"};
}

// Goal: four posts plus two crossbars, all steel. Names carry the side in
// coordinates rather than in "left"/"right", which flips with the attacking
// direction.
void Pitch::BuildGoal(std::span<geometry::Collider> target) const {
  using geometry::Capsule;
  // Laws of the Game cap the post and crossbar section at 12 cm.
  constexpr float kPostRadius = 0.06f;
  const float x = half_length();
  const float y = goal_half_width();
  const float z = goal_height();
  target[0] = {Capsule{math::Vector3(-x, -y, 0), math::Vector3(-x, -y, z), kPostRadius},
               post_material_, "goal_neg_x_neg_y_post"};
  target[1] = {Capsule{math::Vector3(-x, y, 0), math::Vector3(-x, y, z), kPostRadius},
               post_material_, "goal_neg_x_pos_y_post"};
  target[2] = {Capsule{math::Vector3(x, -y, 0), math::Vector3(x, -y, z), kPostRadius},
               post_material_, "goal_pos_x_neg_y_post"};
  target[3] = {Capsule{math::Vector3(x, y, 0), math::Vector3(x, y, z), kPostRadius},
               post_material_, "goal_pos_x_pos_y_post"};
  target[4] = {Capsule{math::Vector3(-x, -y, z), math::Vector3(-x, y, z), kPostRadius},
               post_material_, "goal_neg_x_crossbar"};
  target[5] = {Capsule{math::Vector3(x, -y, z), math::Vector3(x, y, z), kPostRadius},
               post_material_, "goal_pos_x_crossbar"};
}

}  // namespace football::pitch

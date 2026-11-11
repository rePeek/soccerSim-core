#pragma once

// football::pitch owns the pitch physics scene: the read-only description, the
// fixed environment (gravity, ground) and the static colliders generated
// once at construction.
//
// The dependency points one way: ball/player read this scene; pitch never knows
// about them. Rule decisions (goal, out of play, fouls) stay in the rules
// module -- pitch only provides the geometry those decisions need.

#include <span>
#include <vector>

#include "football/foundation/geometry/collider.hpp"
#include "football/models.h"

namespace football::pitch {

// Read-only pitch description in metres: x is length, y is width, and the
// centre spot is the origin.
//
// Ground parameters keep the legacy empirical values, not standard Coulomb
// coefficients, so migration to Pitch preserves the historical simulation
// behaviour exactly.
class Pitch {
 public:
  explicit Pitch(const PitchModel& model =
      ::football_pitch_model{105.0f, 68.0f, 0.04f, 1.6f, 0.015f});
  const PitchModel& model() const { return model_; }

  constexpr float length() const { return model_.length; }
  constexpr float width() const { return model_.width; }
  constexpr float half_length() const { return model_.length * 0.5f; }
  constexpr float half_width() const { return model_.width * 0.5f; }
  constexpr float line_half_width() const { return line_width_ * 0.5f; }
  constexpr float goal_half_width() const { return goal_width_ * 0.5f; }
  constexpr float goal_height() const { return goal_height_; }
  constexpr float goal_depth() const { return goal_depth_; }
  // Penalty-area geometry used by the referee and restart placement.
  constexpr float penalty_area_depth() const { return penalty_area_depth_; }
  constexpr float penalty_mark_distance() const { return penalty_mark_distance_; }

  // ---- Fixed environment the bodies move in (not per-tick input) ----
  // Vertical gravity acceleration along -Z, in m/s^2.
  constexpr float gravity() const { return gravity_; }
  constexpr float quadratic_resistance() const { return model_.quadratic_resistance; }
  constexpr float ground_deceleration() const { return model_.ground_deceleration; }
  // Quadratic air-drag coefficient: a = -air_drag * |v| * v.
  constexpr float air_drag() const { return model_.air_drag; }

  // ---- Physics scene: ground, posts and crossbars, generated once ----
  // Stable ids (also the deterministic CCD tie-break order) and a fixed order.
  std::span<const geometry::ColliderMotion> colliders() const {
    return colliders_;
  }

  // Ground-plane bounds only. Ball radius, line crossing and restart decisions
  // belong to the runtime simulation and referee, not this domain description.
  constexpr bool contains(float x, float y) const {
    return x >= -half_length() && x <= half_length() &&
           y >= -half_width() && y <= half_width();
  }

  // Equality compares the configuration only. The collider list is derived data
  // rebuilt from the configuration in the constructor, so it is not part of a
  // pitch's identity.
  bool operator==(const Pitch& other) const;

private:
  // The whole immutable scene, built once at construction. The generator and
  // its stable collider ids are implementation detail in pitch.cpp.
  void BuildColliders();
  void BuildGrass();
  void BuildGoal();

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
  std::vector<geometry::ColliderMotion> colliders_;
};

}  // namespace football::pitch

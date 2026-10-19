#ifndef FOOTBALL_PITCH_PITCH_HPP
#define FOOTBALL_PITCH_PITCH_HPP

// football::pitch owns the pitch physics scene: the read-only description, the
// fixed environment (gravity, wind, ground) and the static colliders generated
// once at construction.
//
// The dependency points one way: ball/player read this scene; pitch never knows
// about them. Rule decisions (goal, out of play, fouls) stay in the rules
// module -- pitch only provides the geometry those decisions need.

#include <span>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/geometry/collider.hpp"

namespace football::pitch {

// Read-only pitch description in metres: x is length, y is width, and the
// centre spot is the origin.
//
// Ground parameters keep the legacy empirical values, not standard Coulomb
// coefficients, so migration to Pitch preserves the historical simulation
// behaviour exactly.
class Pitch {
 public:
  // Core geometry and ground physics are constructor-required; remaining
  // dimensions keep their legacy defaults. Invalid (non-finite or
  // non-positive geometry / negative friction) values are rejected here so
  // every consumer shares one validated description.
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
  // Penalty-area geometry used by the referee and restart placement.
  constexpr float penalty_area_depth() const { return penalty_area_depth_; }
  constexpr float penalty_mark_distance() const { return penalty_mark_distance_; }

  // ---- Fixed environment the bodies move in (not per-tick input) ----
  const football::math::Vector3& gravity() const { return gravity_; }
  // Reserved: wind is not consumed by the current physics.
  const football::math::Vector3& wind_velocity() const { return wind_velocity_; }
  constexpr float quadratic_resistance() const { return quadratic_resistance_; }
  constexpr float ground_deceleration() const { return ground_deceleration_; }
  constexpr float grass_height() const { return grass_height_; }

  // ---- Physics scene: ground, posts and crossbars, generated once ----
  // Stable ids (also the deterministic CCD tie-break order) and a fixed order.
  std::span<const football::geometry::ColliderMotion> colliders() const {
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

}  // namespace football::pitch

#endif  // FOOTBALL_PITCH_PITCH_HPP

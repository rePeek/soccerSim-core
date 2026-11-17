#pragma once

#include <optional>
#include <span>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/models.h"

namespace football::pitch { class Pitch; }
namespace football::collision { struct CollisionHit; }

namespace football::ball {

using namespace football::pitch;

struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;
};

struct BallContact {
  geometry::ColliderId collider = 0;
  float toi = 0.0f;
  // World-space point on the ball surface: centre - normal * radius.
  math::Vector3 point;
  math::Vector3 normal;
  math::Vector3 relative_velocity;
  float normal_impulse = 0.0f;
  bool position_corrected = false;
  math::Vector3 surface_velocity = math::Vector3(0);
};

struct BallStepResult {
  BallState state;
  // The tick's single impact, or nullopt if none. Overlap repairs stay internal.
  std::optional<BallContact> contact;
};

class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{0.43f, 0.11f, 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  BallStepResult Step(const Pitch& pitch);

  // Apply impulse [N*s] at a world-space point to this ball immediately.
  // Changes velocity and spin, not position or orientation.
  void ApplyImpulseAtPoint(const math::Vector3& impulse,
                           const math::Vector3& world_point);

  struct ContactImpulse {
    math::Vector3 impulse{0};
    float normal_impulse = 0.0f;
  };

  // Compute an impulse from this ball's state without applying it.
  ContactImpulse SolveContactImpulse(
      const BallContact& contact,
      const geometry::ContactMaterial& material) const;

  // Advance this ball against an explicit collider set and commit the result.
  BallStepResult AdvanceBallTick(
      std::span<const geometry::ColliderMotion> colliders,
      float dt, const Pitch& pitch);

  // z is forced to the radius; velocity/spin are cleared.
  void PlaceOnGround(float x, float y);

 private:
  // Thin-shell sphere inertia, I = 2/3 * mass * radius^2.
  float inertia() const {
    return (2.0f / 3.0f) * model_.mass * model_.radius * model_.radius;
  }

  bool OnGround(const BallState& state) const;
  static bool VerticallyResting(const BallState& state);
  math::Vector3 GrassDrag(const math::Vector3& velocity, float span,
                          const Pitch& pitch) const;
  math::Vector3 LaunchVelocity(const BallState& state, float span,
                               const Pitch& pitch) const;
  math::Vector3 Acceleration(const math::Vector3& velocity,
                             const math::Vector3& omega,
                             const Pitch& pitch) const;
  static math::Quaternion IntegrateOrientation(const math::Quaternion& q,
                                              const math::Vector3& omega,
                                              float dt);
  BallState AdvanceFreeFlight(const BallState& state, float span,
                              const Pitch& pitch) const;
  BallContact ToBallContact(const collision::CollisionHit& hit,
                            const math::Vector3& velocity) const;
  std::optional<BallContact> AdvanceBallSegment(
      std::span<const geometry::ColliderMotion> colliders,
      float dt, const Pitch& pitch);

  static constexpr float kTouchEpsilon = 1e-4f;
  BallModel model_;
  BallState state_;
};

}  // namespace football::ball

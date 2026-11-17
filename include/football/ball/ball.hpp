#pragma once


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
  // Fraction along the predicted path; not a fractional integration timestep.
  float toi = 0.0f;
  // World-space point on the ball surface: centre - normal * radius.
  math::Vector3 point;
  math::Vector3 normal;
  // Predicted full-tick centre velocity relative to the collider surface.
  math::Vector3 relative_velocity;
  bool position_corrected = false;
  math::Vector3 surface_velocity = math::Vector3(0);
};
class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{0.43f, 0.11f, 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  // Predict free motion, sweep the path, then commit or stop at one impact.
  // An impact uses the predicted velocity/spin, with no post-impact motion.
  void Step(const Pitch& pitch);

  // Apply impulse [N*s] at a world-space point to this ball immediately.
  // Changes velocity and spin, not position or orientation.
  void ApplyImpulseAtPoint(const math::Vector3& impulse,
                           const math::Vector3& world_point);

  // Impulse [N*s] to apply at contact.point, or nullopt when the contact is
  // already separating. Does not touch the ball.
  std::optional<math::Vector3> SolveContactImpulse(
      const BallContact& contact,
      const geometry::ContactMaterial& material) const;

  // z is forced to the radius; velocity/spin are cleared.
  void PlaceOnGround(float x, float y);

 private:
  // Thin-shell sphere inertia, I = 2/3 * mass * radius^2.
  float inertia() const {
    return (2.0f / 3.0f) * model_.mass * model_.radius * model_.radius;
  }

  BallContact ToBallContact(const collision::CollisionHit& hit,
                            const math::Vector3& velocity) const;
  std::optional<collision::CollisionHit> FindContact(
      const BallState& start, const BallState& next,
      std::span<const geometry::ColliderMotion> colliders, float dt,
      std::span<const geometry::ColliderId> repaired) const;
  void Depenetrate(BallState& state, const BallContact& contact,
                   std::span<const geometry::ColliderMotion> colliders) const;

  BallModel model_;
  BallState state_;
};

}  // namespace football::ball

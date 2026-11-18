#pragma once

#include <optional>

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/models.h"

namespace football::pitch { class Pitch; }

namespace football::ball {

using namespace football::pitch;

struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;
};

struct BallContact {
  // Fraction along the predicted path; not a fractional integration timestep.
  float toi = 0.0f;
  // World-space point on the ball surface: centre - normal * radius.
  math::Vector3 point;
  math::Vector3 normal;
  // Centre velocity relative to the collider surface at contact time.
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
  // An impact is resolved at the contact instant, and the rest of the tick is
  // deliberately not integrated.
  void Step(const Pitch& pitch);

  // Apply impulse [N*s] at a world-space point on the ball surface. Changes
  // velocity and spin, not position or orientation; the point must be one
  // radius from the centre (asserted), since only the shell can be struck.
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
  BallModel model_;
  BallState state_;
};

}  // namespace football::ball

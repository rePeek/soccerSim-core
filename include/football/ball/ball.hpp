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

class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{0.43f, 0.11f, 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  // Predict free motion, sweep the path, then commit or stop at one impact.
  // An impact stops at the contact point and resolves the impulse from the
  // predicted state; the rest of the tick is not integrated.
  void Step(const Pitch& pitch);

  // Apply impulse [N*s] at a world-space point on the ball surface. Changes
  // velocity and spin, not position or orientation; the point must be one
  // radius from the centre (asserted), since only the shell can be struck.
  void ApplyImpulseAtPoint(const math::Vector3& impulse,
                           const math::Vector3& world_point);

  // Impulse [N*s] to apply at `contact_point`, or nullopt when the contact is
  // already separating. `normal` points from the surface toward the ball and
  // `contact_point` sits on the ball's shell. Does not touch the ball.
  std::optional<math::Vector3> SolveContactImpulse(
      const math::Vector3& normal,
      const math::Vector3& contact_point,
      const math::Vector3& surface_velocity,
      const geometry::ContactMaterial& material) const;

  // z is forced to the radius; velocity/spin are cleared.
  void PlaceOnGround(float x, float y);

 private:
  BallModel model_;
  BallState state_;
};

}  // namespace football::ball

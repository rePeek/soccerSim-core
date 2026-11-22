#pragma once

#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/models.h"

namespace football::pitch { class Pitch; }

namespace football::ball {

using namespace football::pitch;

class Ball {
 public:
  explicit Ball(const BallModel& model =
      ::football_ball_model{.mass = 0.43f, .radius = 0.11f, .magnus_coefficient = 0.02f});

  BallState get_state() const { return state_; }
  void set_state(const BallState& state) { state_ = state; }
  const BallModel& model() const { return model_; }

  void Step(const Pitch& pitch);

  void ApplyImpulseAtPoint(const math::Vector3& impulse,
                           const math::Vector3& world_point);

  std::optional<math::Vector3> SolveContactImpulse(
      const math::Vector3& normal,
      const math::Vector3& contact_point,
      const math::Vector3& surface_velocity,
      const geometry::ContactMaterial& material) const;

  void PlaceOnGround(float x, float y);

 private:
  BallModel model_;
  BallState state_;
};

}

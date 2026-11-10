#pragma once

#include <span>
#include <vector>

#include "football/ball/ball.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball { class BallModel; }
namespace football::pitch { class Pitch; }

namespace football::ball {

struct BallContact {
  football::geometry::ColliderId collider = 0;
  float toi = 0.0f;
  football::math::Vector3 point;
  football::math::Vector3 normal;
  football::math::Vector3 relative_velocity;
  float normal_impulse = 0.0f;
  bool position_corrected = false;
  football::math::Vector3 surface_velocity = football::math::Vector3(0);
};

struct BallStepResult {
  BallState state;
  std::vector<BallContact> contacts;
};

BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const football::geometry::ColliderMotion> colliders,
                               float dt,
                               const football::ball::BallModel& config,
                               const football::pitch::Pitch& pitch);

}

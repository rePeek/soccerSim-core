#pragma once

#include <span>
#include <vector>

#include "football/ball/ball.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::pitch { class Pitch; }

namespace football::ball {

using namespace football::pitch;

struct BallContact {
  geometry::ColliderId collider = 0;
  float toi = 0.0f;
  math::Vector3 point;
  math::Vector3 normal;
  math::Vector3 relative_velocity;
  float normal_impulse = 0.0f;
  bool position_corrected = false;
  math::Vector3 surface_velocity = math::Vector3(0);
};

struct BallStepResult {
  BallState state;
  // At most one entry carries a non-zero normal impulse: a tick resolves one
  // impact and integrates the rest of itself collision-free. Any further
  // entries are zero-impulse overlap repairs recorded before that impact.
  std::vector<BallContact> contacts;
};

BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const geometry::ColliderMotion> colliders,
                               float dt,
                               const BallModel& config,
                               const Pitch& pitch);

}

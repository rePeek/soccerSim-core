#pragma once

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

// Authoritative ball state: pose and motion as one plain value. Behaviour lives
// in Ball; consumers that only record or transmit a state depend on this header
// instead of on the physics class.
struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;

  bool operator==(const BallState&) const = default;
};

}  // namespace football::ball

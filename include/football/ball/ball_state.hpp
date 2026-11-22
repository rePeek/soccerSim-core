#pragma once

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

struct BallState {
  math::Vector3 position;
  math::Vector3 velocity;
  math::Vector3 angular_velocity;
  math::Quaternion orientation;

  bool operator==(const BallState&) const = default;
};

}

#pragma once

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

struct BallState {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
  football::math::Vector3 angular_velocity;
  football::math::Quaternion orientation;
};

struct BallEndpointConstraint {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
};

}

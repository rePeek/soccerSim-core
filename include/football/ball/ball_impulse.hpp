#pragma once

#include "football/foundation/math/vector3.hpp"

namespace football::ball {

struct BallImpulse {
  football::math::Vector3 impulse = football::math::Vector3(0);
  football::math::Vector3 contact_point = football::math::Vector3(0);
};

}

#pragma once

#include <cstddef>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

// One renderable frame: the part of BallState a caller copies out, position and
// orientation only. It is written with the math types directly, so the
// simulation never converts, and the asserts below pin the bytes a consumer
// maps onto its own struct: seven consecutive 32-bit floats, no padding.
struct BallSnapshot {
  math::Vector3 position;
  math::Quaternion orientation;
};

static_assert(sizeof(BallSnapshot) == 28, "ball snapshot frame size");
static_assert(offsetof(BallSnapshot, orientation) == 12,
              "ball snapshot frame offset");

}  // namespace football::ball

#pragma once

#include "football/foundation/math/vector3.hpp"
#include "football/animation/types.hpp"

// Quantized outgoing movement (velocity plus angle). A pure value type shared by
// the Player's candidate filter and the offline baker's legacy import; it carries
// no selection logic of its own.
struct Quadrant {
  int id = 0;
  football::math::Vector3 position;
  e_Velocity velocity;
  football::math::radian angle;
};

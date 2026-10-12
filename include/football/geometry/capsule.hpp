#ifndef FOOTBALL_GEOMETRY_CAPSULE_HPP
#define FOOTBALL_GEOMETRY_CAPSULE_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Capsule {
  blunted::Vector3 a;
  blunted::Vector3 b;
  float radius = 0.0f;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_CAPSULE_HPP

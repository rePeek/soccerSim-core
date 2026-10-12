#ifndef FOOTBALL_GEOMETRY_SHAPES_HPP
#define FOOTBALL_GEOMETRY_SHAPES_HPP

// Shared shape primitives. geometry owns what things look like; collision
// (football/foundation/collision) owns whether two shapes touch. No physics
// (mass, friction, response) lives here.

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Plane {
  football::math::Vector3 point;
  football::math::Vector3 normal;
};

struct Sphere {
  football::math::Vector3 center;
  float radius = 0.0f;
};

struct Capsule {
  football::math::Vector3 a;
  football::math::Vector3 b;
  float radius = 0.0f;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_SHAPES_HPP

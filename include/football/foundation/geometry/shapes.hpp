#ifndef FOOTBALL_GEOMETRY_SHAPES_HPP
#define FOOTBALL_GEOMETRY_SHAPES_HPP

// Shared shape primitives. geometry owns what things look like; collision
// (football/foundation/collision) owns whether two shapes touch. No physics
// (mass, friction, response) lives here.

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Plane {
  blunted::Vector3 point;
  blunted::Vector3 normal;
};

struct Sphere {
  blunted::Vector3 center;
  float radius = 0.0f;
};

struct Capsule {
  blunted::Vector3 a;
  blunted::Vector3 b;
  float radius = 0.0f;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_SHAPES_HPP

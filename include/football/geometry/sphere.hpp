#ifndef FOOTBALL_GEOMETRY_SPHERE_HPP
#define FOOTBALL_GEOMETRY_SPHERE_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Sphere {
  blunted::Vector3 center;
  float radius = 0.0f;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_SPHERE_HPP

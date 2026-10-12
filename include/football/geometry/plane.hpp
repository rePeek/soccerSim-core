#ifndef FOOTBALL_GEOMETRY_PLANE_HPP
#define FOOTBALL_GEOMETRY_PLANE_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Plane {
  blunted::Vector3 point;
  blunted::Vector3 normal;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_PLANE_HPP

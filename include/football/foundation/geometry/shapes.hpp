#pragma once



#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

struct Plane {
  math::Vector3 point;
  math::Vector3 normal;
  bool operator==(const Plane&) const = default;
};

struct HalfSpace {
  math::Vector3 point;
  math::Vector3 normal;
  bool operator==(const HalfSpace&) const = default;
};

struct Sphere {
  math::Vector3 center;
  float radius = 0.0f;
  bool operator==(const Sphere&) const = default;
};

struct Capsule {
  math::Vector3 a;
  math::Vector3 b;
  float radius = 0.0f;
  bool operator==(const Capsule&) const = default;
};

}

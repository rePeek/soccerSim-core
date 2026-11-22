#pragma once


#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::collision {

struct MovingSphere {
  geometry::Sphere shape;
  math::Vector3 velocity;
};

struct CollisionHit {
  float toi = 0.0f;
  math::Vector3 center;
  math::Vector3 normal;
  math::Vector3 surface_velocity;
};

struct ColliderHit {
  CollisionHit hit;
  const geometry::Collider* collider = nullptr;
};

}

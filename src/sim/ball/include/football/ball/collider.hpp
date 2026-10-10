#ifndef FOOTBALL_BALL_COLLIDER_HPP
#define FOOTBALL_BALL_COLLIDER_HPP

#include <cstdint>
#include <variant>

#include "foundation/math/vector3.hpp"

namespace football::ball {

using ColliderId = std::uint32_t;

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

using ColliderShape = std::variant<Plane, Sphere, Capsule>;

struct ContactMaterial {
  float restitution = 0.5f;
  float friction = 0.0f;
};

// Static collider: geometry + material. Id is used for stable, deterministic
// ordering; it carries no Player/Team/rules identity.
struct Collider {
  ColliderId id = 0;
  ColliderShape shape;
  ContactMaterial material;
};

// One collider's motion over a single tick. `start` and `end` are the shapes at
// t and t+1; static colliders set end == start. This stage assumes linear
// translation only (no rotation of the collider itself).
struct ColliderMotion {
  ColliderId id = 0;
  ColliderShape start;
  ColliderShape end;
  ContactMaterial material;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_COLLIDER_HPP

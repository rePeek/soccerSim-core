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

// One tick, t in [0,1]. Sphere center, capsule endpoints and radii interpolate
// LINEARLY between start/end. Different endpoint displacements rotate/stretch
// the capsule axis; this is not quaternion interpolation or an inferred rigid
// angular trajectory. Split the input tape if an animation needs an arc.
// Surface transport is minimal-twist: blend endpoint velocities at the closest
// axis parameter, plus omega x (radius*normal) with omega=d x d_dot / |d|^2,
// and radius_dot*normal. No axial spin is inferable from an axis; it is zero.
// At a collapsed axis omega is zero. Primitive kind must stay fixed. Planes
// remain static. A static sphere/capsule has end == start.
struct ColliderMotion {
  ColliderId id = 0;
  ColliderShape start;
  ColliderShape end;
  ContactMaterial material;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_COLLIDER_HPP

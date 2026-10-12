#ifndef FOOTBALL_GEOMETRY_COLLIDER_HPP
#define FOOTBALL_GEOMETRY_COLLIDER_HPP

// Shared collision-geometry contract: shapes, their per-tick motion and the
// contact material. Owned by no single domain: ball, pitch and the player body
// all produce or consume these values. No ball/rules semantics live here.

#include <cstdint>
#include <variant>

#include "football/geometry/capsule.hpp"
#include "football/geometry/plane.hpp"
#include "football/geometry/sphere.hpp"

namespace football::geometry {

using ColliderId = std::uint32_t;

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

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_COLLIDER_HPP

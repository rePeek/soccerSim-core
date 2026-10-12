#ifndef FOOTBALL_GEOMETRY_COLLIDER_HPP
#define FOOTBALL_GEOMETRY_COLLIDER_HPP

// Collision-geometry contract: shapes tagged for deterministic ordering, plus
// their per-tick motion and contact material. Owned by no single domain: ball,
// pitch and the player body all produce or consume these values. No ball/rules
// semantics live here.
//
// ContactMaterial deliberately lives in geometry for now: the producers (pitch,
// player body) already attach it to the collider they emit, and collision never
// reads it, so a separate material/physics module would add a hop without a
// consumer. Revisit once the player body shares the generic collision path and
// the right home for shared contact materials is clear.

#include <cstdint>
#include <variant>

#include "football/foundation/geometry/shapes.hpp"

namespace football::geometry {

using ColliderId = std::uint32_t;

using ColliderShape = std::variant<Plane, HalfSpace, Sphere, Capsule>;

struct ContactMaterial {
  float restitution = 0.5f;
  float friction = 0.0f;
  bool operator==(const ContactMaterial&) const = default;
};

// Static collider: geometry + material. Id is used for stable, deterministic
// ordering; it carries no Player/Team/rules identity.
struct Collider {
  ColliderId id = 0;
  ColliderShape shape;
  ContactMaterial material;
  bool operator==(const Collider&) const = default;
};

// One tick, t in [0,1]. Sphere center, capsule endpoints and radii interpolate
// LINEARLY between start/end. Different endpoint displacements rotate/stretch
// the capsule axis; this is not quaternion interpolation or an inferred rigid
// angular trajectory. Split the input tape if an animation needs an arc.
// Surface transport is minimal-twist: blend endpoint velocities at the closest
// axis parameter, plus omega x (radius*normal) with omega=d x d_dot / |d|^2,
// and radius_dot*normal. No axial spin is inferable from an axis; it is zero.
// At a collapsed axis omega is zero. Primitive kind must stay fixed. Planes
// and half-spaces remain static. A static sphere/capsule has end == start.
struct ColliderMotion {
  ColliderId id = 0;
  ColliderShape start;
  ColliderShape end;
  ContactMaterial material;
  bool operator==(const ColliderMotion&) const = default;
};

}  // namespace football::geometry

#endif  // FOOTBALL_GEOMETRY_COLLIDER_HPP

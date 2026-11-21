#pragma once


// Generic continuous collision queries. Depends only on geometry + foundation;
// knows nothing about ball, pitch or any material/response model.

#include <optional>
#include <span>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"

namespace football::collision {

// Closest point on segment [a, b] to p. Shared by the sweep kernels and by
// callers that need the same projection (e.g. capsule depenetration).
math::Vector3 ClosestPointOnSegment(const math::Vector3& p,
                                       const math::Vector3& a,
                                       const math::Vector3& b);

// Moves a sphere centre back onto the surface of a shape it overlaps: returns
// the centre of a sphere of `radius` resting tangent on `shape`, with `normal`
// pointing from the surface toward the sphere. Pure geometry, no time or toi.
math::Vector3 Depenetrate(const math::Vector3& center, float radius,
                          const geometry::ColliderShape& shape,
                          const math::Vector3& normal);

bool Overlap(const geometry::Sphere& sphere,
             const geometry::Sphere& other);
bool Overlap(const geometry::Sphere& sphere,
             const geometry::Capsule& capsule);
// Two-sided: true when the centre is within one radius of the surface.
bool Overlap(const geometry::Sphere& sphere,
             const geometry::Plane& plane);
// One-sided: true when the sphere reaches into the solid (-normal) side.
bool Overlap(const geometry::Sphere& sphere,
             const geometry::HalfSpace& half_space);

// Pure swept-sphere CCD against one shape over [0, dt] (`dt` in seconds). The
// shape is all it takes: names, materials and owners stay outside.
// Deterministic, never mutates state and draws no RNG.
//
// Input contract: dt must be finite and > 0, radii must be finite and >= 0, and
// all coordinates must be finite. A violation throws std::invalid_argument (a
// malformed shape is a caller bug, not 'no collision'). Valid input with no
// contact over the interval returns nullopt. Planes are two-sided surfaces;
// half-spaces are one-sided solids.
std::optional<CollisionHit> Sweep(
    const MovingSphere& sphere,
    const geometry::ColliderShape& shape, float dt);

// Earliest contact over a set, with the collider that owns it. Equal TOI keeps
// the collider met first in the set, so only names and addresses are ignored.
std::optional<ColliderHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const geometry::Collider> colliders, float dt);

}  // namespace football::collision

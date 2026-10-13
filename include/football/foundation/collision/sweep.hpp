#ifndef FOOTBALL_COLLISION_SWEEP_HPP
#define FOOTBALL_COLLISION_SWEEP_HPP

// Generic continuous collision queries. Depends only on geometry + foundation;
// knows nothing about ball, pitch, player or any material/response model.

#include <optional>
#include <span>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"

namespace football::collision {

// Closest point on segment [a, b] to p. Shared by the sweep kernels and by
// callers that need the same projection (e.g. capsule depenetration).
football::math::Vector3 ClosestPointOnSegment(const football::math::Vector3& p,
                                       const football::math::Vector3& a,
                                       const football::math::Vector3& b);

bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Sphere& other);
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Capsule& capsule);
// Two-sided: true when the centre is within one radius of the surface.
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Plane& plane);
// One-sided: true when the sphere reaches into the solid (-normal) side.
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::HalfSpace& half_space);

// ---- Static penetration (strict) ----
// Same geometry as Overlap(), but exact tangency is *not* penetration. The
// legacy player body test uses a strict comparison, so these exist to reuse the
// shared shape geometry without silently changing its tangency semantics.
bool Penetrates(const football::geometry::Sphere& sphere,
                const football::geometry::Sphere& other);
bool Penetrates(const football::geometry::Sphere& sphere,
                const football::geometry::Capsule& capsule);

// Pure swept-sphere CCD against one collider over [0, dt] (`dt` in seconds).
// Deterministic, never mutates state and draws no RNG.
//
// Input contract: dt must be finite and > 0, radii must be finite and >= 0,
// all coordinates must be finite, and collider.start/end must hold the same
// shape kind. A violation throws std::invalid_argument (a malformed collider
// is a caller bug, not 'no collision'). Valid input with no contact over the
// interval returns nullopt. Planes are two-sided surfaces; half-spaces are
// one-sided solids.
std::optional<CollisionHit> Sweep(
    const MovingSphere& sphere,
    const football::geometry::ColliderMotion& collider, float dt);

// Earliest hit over a set. Equal TOI is broken by the lower collider id.
std::optional<CollisionHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const football::geometry::ColliderMotion> colliders, float dt);

}  // namespace football::collision

#endif  // FOOTBALL_COLLISION_SWEEP_HPP

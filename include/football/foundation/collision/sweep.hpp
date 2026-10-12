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
blunted::Vector3 ClosestPointOnSegment(const blunted::Vector3& p,
                                       const blunted::Vector3& a,
                                       const blunted::Vector3& b);

// ---- Static overlap (no time component) ----
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Sphere& other);
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Capsule& capsule);
bool Overlap(const football::geometry::Sphere& sphere,
             const football::geometry::Plane& plane);

// ---- Continuous queries ----
// Pure swept-sphere CCD against one collider over [0, dt] (`dt` in seconds).
// Deterministic, never mutates state and draws no RNG.
std::optional<CollisionHit> Sweep(
    const MovingSphere& sphere,
    const football::geometry::ColliderMotion& collider, float dt);

// Earliest hit over a set. Equal TOI is broken by the lower collider id.
std::optional<CollisionHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const football::geometry::ColliderMotion> colliders, float dt);

}  // namespace football::collision

#endif  // FOOTBALL_COLLISION_SWEEP_HPP

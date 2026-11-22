#pragma once



#include <optional>
#include <span>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"

namespace football::collision {

math::Vector3 ClosestPointOnSegment(const math::Vector3& p,
                                       const math::Vector3& a,
                                       const math::Vector3& b);

math::Vector3 Depenetrate(const math::Vector3& center, float radius,
                          const geometry::ColliderShape& shape,
                          const math::Vector3& normal);

bool Overlap(const geometry::Sphere& sphere,
             const geometry::Sphere& other);
bool Overlap(const geometry::Sphere& sphere,
             const geometry::Capsule& capsule);
bool Overlap(const geometry::Sphere& sphere,
             const geometry::Plane& plane);
bool Overlap(const geometry::Sphere& sphere,
             const geometry::HalfSpace& half_space);

std::optional<CollisionHit> Sweep(
    const MovingSphere& sphere,
    const geometry::ColliderShape& shape, float dt);

std::optional<ColliderHit> FirstHit(
    const MovingSphere& sphere,
    std::span<const geometry::Collider> colliders, float dt);

}

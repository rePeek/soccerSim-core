#ifndef FOOTBALL_COLLISION_SWEEP_DEFORMING_CAPSULE_HPP
#define FOOTBALL_COLLISION_SWEEP_DEFORMING_CAPSULE_HPP

#include <optional>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"

namespace football::collision::detail {

// Endpoint-linear, radius-linear capsule motion. No actor/animation dependency.
std::optional<CollisionHit> SweepDeformingCapsule(
    const MovingSphere& sphere, const football::geometry::ColliderMotion& collider,
    const football::geometry::Capsule& start, const football::geometry::Capsule& end,
    float dt);

}  // namespace football::collision::detail

#endif  // FOOTBALL_COLLISION_SWEEP_DEFORMING_CAPSULE_HPP

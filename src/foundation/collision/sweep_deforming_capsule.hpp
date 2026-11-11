#pragma once


#include <optional>

#include "football/foundation/collision/collision_hit.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"

namespace football::collision::detail {

// Endpoint-linear, radius-linear capsule motion. No actor/animation dependency.
std::optional<CollisionHit> SweepDeformingCapsule(
    const MovingSphere& sphere, const geometry::ColliderMotion& collider,
    const geometry::Capsule& start, const geometry::Capsule& end,
    float dt);

}  // namespace football::collision::detail

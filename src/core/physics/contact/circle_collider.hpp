#ifndef _HPP_CORE_PHYSICS_CONTACT_CIRCLE_COLLIDER
#define _HPP_CORE_PHYSICS_CONTACT_CIRCLE_COLLIDER

#include "foundation/math/vector3.hpp"

// Contact geometry and contact facts live in `football::contact`: they are
// consumed by Physics, Action, Rules/Events and Replay, not just by one solver.
namespace football::contact {

// Planar circle used as ordinary player-body contact geometry.
//
// This is a Derived value (Profile + State -> shape), rebuilt on demand and
// never authoritative. It deliberately has no dependency on PlayerBase,
// Humanoid, PlayerData or any other onthepitch type, so the contact core sits
// above the legacy layer instead of below it.
struct CircleCollider {
  blunted::Vector3 center = blunted::Vector3(0);
  float radius = 0.0f;

  void SetCenter(const blunted::Vector3 &position) { center = position.Get2D(); }

  bool Intersects(const CircleCollider &other) const {
    return (center - other.center).Get2D().GetLength() < radius + other.radius;
  }

  void Mirror() { center.Mirror(); }
};

}  // namespace football::contact

#endif  // _HPP_CORE_PHYSICS_CONTACT_CIRCLE_COLLIDER

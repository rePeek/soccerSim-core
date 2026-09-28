#ifndef _HPP_CORE_PHYSICS_CONTACT_PLAYER_COLLIDER
#define _HPP_CORE_PHYSICS_CONTACT_PLAYER_COLLIDER

#include "core/domain/player/player_profile.hpp"
#include "core/physics/contact/circle_collider.hpp"
#include "core/state/player_state.hpp"

// Canonical mapping from a player entity to contact geometry.
//
// Radius is an inherent property of the player; the center is current
// kinematics. Neither is stored authoritatively anywhere else. Legacy code may
// keep a serialized copy as a compatibility shadow, but new simulation and
// contact consumers must build the shape through this function.
inline CircleCollider BuildPlayerGroundCollider(
    const football::domain::PlayerProfile &profile,
    const PlayerState &state) {
  CircleCollider collider;
  collider.center = state.position.Get2D();
  collider.radius = profile.physical.bodyRadius;
  return collider;
}

#endif  // _HPP_CORE_PHYSICS_CONTACT_PLAYER_COLLIDER

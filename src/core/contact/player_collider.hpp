#ifndef _HPP_CORE_CONTACT_PLAYER_COLLIDER
#define _HPP_CORE_CONTACT_PLAYER_COLLIDER

#include "core/model/player/player_profile.hpp"
#include "core/contact/circle_collider.hpp"
#include "core/model/player/player.hpp"

namespace football::contact {

// Canonical mapping from a player entity to contact geometry.
//
// Radius is an inherent property of the player; the center is current
// kinematics. Neither is stored authoritatively anywhere else. Legacy code may
// keep a serialized copy as a compatibility shadow, but new simulation and
// contact consumers must build the shape through this function.
inline CircleCollider BuildPlayerGroundCollider(
    const football::model::PlayerProfile &profile,
    const PlayerState &state) {
  CircleCollider collider;
  collider.center = state.position.Get2D();
  collider.radius = profile.physical.bodyRadius;
  return collider;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_PLAYER_COLLIDER

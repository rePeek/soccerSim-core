#ifndef FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP
#define FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP

#include "core/physics/contact/geometry/circle_collider.hpp"
#include "core/model/player/player.hpp"

namespace football_sim::contact {

inline CircleCollider BuildPlayerGroundCollider(
    const football_sim::Player& player) {
  CircleCollider collider;
  collider.center = player.Position().Get2D();
  collider.radius = player.BodyRadius();
  return collider;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP

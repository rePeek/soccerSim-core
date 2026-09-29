#ifndef FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP
#define FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP

#include "core/contact/circle_collider.hpp"
#include "core/model/player/player.hpp"

namespace football::contact {

inline CircleCollider BuildPlayerGroundCollider(
    const football::model::Player& player) {
  CircleCollider collider;
  collider.center = player.Position().Get2D();
  collider.radius = player.BodyRadius();
  return collider;
}

}  // namespace football::contact

#endif  // FOOTBALL_CORE_CONTACT_PLAYER_COLLIDER_HPP

#ifndef _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT
#define _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT

#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/contact/player_body_collider.hpp"
#include "core/contact/sphere_capsule_contact.hpp"

namespace football::contact {

// Passive player-body vs ball contact (7G-7a). The body is a fixed trio of
// capsules built from the player's ground position; the ball is a sphere.
// Geometry only, with a deterministic part order (upper, lower, head). Knows
// nothing about Match, possession, actions, the referee or GetStat().
inline std::optional<BallContact> DetectPlayerBodyBallContact(
    const PlayerBodyCollider &body,
    const blunted::Vector3 &ballPosition, float ballRadius) {
  DO_VALIDATION;
  const CapsuleCollider *volumes[3] = {&body.upperBody, &body.lowerBody,
                                       &body.head};
  for (const CapsuleCollider *volume : volumes) {
    if (auto contact =
            DetectSphereCapsuleContact(ballPosition, ballRadius, *volume)) {
      return contact;
    }
  }
  return std::nullopt;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT
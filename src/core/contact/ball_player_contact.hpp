#ifndef _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT
#define _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT

#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/contact/player_body_collider.hpp"
#include "core/contact/sphere_capsule_contact.hpp"
#include "core/contact/sweep_sphere_capsule.hpp"

namespace football_sim::contact {

// Passive player-body vs ball contact (7G-7a). The body is a fixed trio of
// capsules built from the player's ground position; the ball is a sphere.
// Geometry only, with a deterministic part order (upper, lower, head). Knows
// nothing about Match, possession, actions, the referee or GetStat().
inline std::optional<BallContact> DetectPlayerBodyBallContact(
    const PlayerBodyCollider &body,
    const football_sim::math::Vector3 &ballPosition, float ballRadius) {

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

// 7G-8-d-a: moving player body vs ball CCD. The player capsule trio is
// treated as static while the ball sweeps the relative chord
//
//   ballDelta - playerDelta
//
// so a closing player shortens the time of impact exactly as expected.
// Geometry only: it returns the earliest timeWithinTick across the three
// body parts but never inspects approach/separation or mutates state.
inline std::optional<BallContact> SweepPlayerBodyBallContact(
    const PlayerBodyCollider &body,
    const football_sim::math::Vector3 &ballStart, const football_sim::math::Vector3 &ballEnd,
    const football_sim::math::Vector3 &playerStart, const football_sim::math::Vector3 &playerEnd,
    float ballRadius, float dt) {

  const football_sim::math::Vector3 relativeDelta =
      (ballEnd - ballStart) - (playerEnd - playerStart);
  const football_sim::math::Vector3 relativeEnd = ballStart + relativeDelta;
  const CapsuleCollider *volumes[3] = {&body.upperBody, &body.lowerBody,
                                       &body.head};
  std::optional<BallContact> earliest;
  for (const CapsuleCollider *volume : volumes) {
    if (auto c = SweepSphereCapsule(ballStart, relativeEnd, ballRadius,
                                    *volume, dt)) {
      if (!earliest || c->timeWithinTick < earliest->timeWithinTick) {
        earliest = c;
      }
    }
  }
  return earliest;
}

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_BALL_PLAYER_CONTACT
#ifndef _HPP_CORE_CONTACT_BALL_GROUND_CONTACT
#define _HPP_CORE_CONTACT_BALL_GROUND_CONTACT

#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/state/ball_state.hpp"

namespace football::contact {

// 7G-7d-a: ground-plane detector. Pure geometry: converts a ball state into a
// BallContact when the ball bottom reaches or crosses the pitch plane (z = 0).
//
// Frozen semantics mirror DetectContact:
//   separated  (bottom > 0)            -> nullopt
//   touching   (bottom == 0)           -> BallContact(penetration = 0)
//   overlap    (bottom < 0)            -> BallContact(penetration > 0)
//
//   normal       (0, 0, 1): away from the plane into the ball
//   point        ball bottom projected onto the plane (z = 0)
//   penetration  radius - position.z
//
// This only reports geometry; it mutates nothing. The caller decides whether
// to run an impulse response and/or positional correction.
inline std::optional<BallContact> DetectBallGroundContact(
    const BallState &state,
    const football::domain::BallProfile &ball) {
  DO_VALIDATION;
  const float bottom = state.position.coords[2] - ball.radius;
  if (bottom > 0.0f) return std::nullopt;

  BallContact contact;
  contact.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
  contact.point = state.position.Get2D();  // (x, y, 0)
  contact.penetration = ball.radius - state.position.coords[2];
  return contact;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_BALL_GROUND_CONTACT
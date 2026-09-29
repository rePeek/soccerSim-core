#ifndef FOOTBALL_CORE_CONTACT_BALL_GROUND_CONTACT_HPP
#define FOOTBALL_CORE_CONTACT_BALL_GROUND_CONTACT_HPP

#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/model/ball/ball.hpp"

namespace football_sim::contact {

// Pure ground-plane geometry. The returned contact is a fact; it does not
// mutate Ball.
inline std::optional<BallContact> DetectBallGroundContact(
    const football_sim::Ball& ball) {
  const football_sim::BallState& state = ball.State();
  const float bottom = state.position.coords[2] - ball.Radius();
  if (bottom > 0.0f) return std::nullopt;

  BallContact contact;
  contact.normal = football_sim::math::Vector3(0.0f, 0.0f, 1.0f);
  contact.point = state.position.Get2D();
  contact.penetration = -bottom;
  return contact;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_CONTACT_BALL_GROUND_CONTACT_HPP

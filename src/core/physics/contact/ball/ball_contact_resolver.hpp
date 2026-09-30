#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_CONTACT_RESOLVER_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_CONTACT_RESOLVER_HPP

#include <algorithm>
#include <cassert>
#include <cmath>

#include "core/model/ball/ball.hpp"
#include "core/physics/contact/ball/ball_contact.hpp"

namespace football_sim::contact {

// Rigid-sphere impulse response. A pure function: it takes the pre-contact
// state and returns the post-impulse state without mutating any owner.
//
//   r     = -normal * radius
//   vc    = (v - surfaceVelocity) + omega x r
//   vn    = dot(vc, normal)
//   Jn    = -(1 + e) m vn
//   Jt    = min((k / (1 + k)) m |vt|, mu Jn)
//   v    += (Jn n + Jt) / m
//   omega += (r x Jt) / I
[[nodiscard]] inline football_sim::BallState ResolveBallContact(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    const BallContact& contact,
    const football_sim::math::Vector3& surfaceVelocity,
    const ContactMaterial& material) {
  assert(ball.Mass() > 0.0f);
  assert(ball.Radius() > 0.0f);
  assert(ball.InertiaFactor() > 0.0f);
  assert(contact.normal.GetLength() > 1e-6f);

  football_sim::BallState next = state;
  const football_sim::math::Vector3 normal = contact.normal.GetNormalized();
  const football_sim::math::Vector3 leverArm = normal * -ball.Radius();
  const football_sim::math::Vector3 contactVelocity =
      state.velocity - surfaceVelocity +
      state.angularVelocity.GetCrossProduct(leverArm);
  const float normalSpeed = contactVelocity.GetDotProduct(normal);
  if (normalSpeed >= 0.0f) {
    return next;
  }

  const float normalImpulse =
      -(1.0f + material.restitution) * ball.Mass() * normalSpeed;
  const football_sim::math::Vector3 normalImpulseVector = normal * normalImpulse;
  const football_sim::math::Vector3 tangentialVelocity =
      contactVelocity - normal * normalSpeed;
  const float tangentialSpeed = tangentialVelocity.GetLength();

  football_sim::math::Vector3 tangentialImpulse(0.0f, 0.0f, 0.0f);
  constexpr float kSpeedEpsilon = 1e-6f;
  if (tangentialSpeed > kSpeedEpsilon) {
    const float stickFactor =
        ball.InertiaFactor() / (1.0f + ball.InertiaFactor());
    const float stickImpulse = stickFactor * ball.Mass() * tangentialSpeed;
    const float frictionLimit = material.friction * std::fabs(normalImpulse);
    const float magnitude = std::min(stickImpulse, frictionLimit);
    tangentialImpulse = tangentialVelocity * (-magnitude / tangentialSpeed);
  }

  next.velocity += (normalImpulseVector + tangentialImpulse) / ball.Mass();
  const float inertia = ball.InertiaFactor() * ball.Mass() * ball.Radius() *
                        ball.Radius();
  next.angularVelocity += leverArm.GetCrossProduct(tangentialImpulse) / inertia;
  return next;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_BALL_CONTACT_RESOLVER_HPP
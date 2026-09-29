#ifndef FOOTBALL_CORE_CONTACT_BALL_CONTACT_RESOLVER_HPP
#define FOOTBALL_CORE_CONTACT_BALL_CONTACT_RESOLVER_HPP

#include <algorithm>
#include <cassert>
#include <cmath>

#include "core/contact/ball_contact.hpp"
#include "core/model/ball/ball.hpp"

namespace football::contact {

// Rigid-sphere impulse response. Geometry and material stay separate from the
// mutable model; the resolver computes next state and commits it via SetState.
struct BallContactResolver {
  static void Resolve(football::model::Ball& ball, const BallContact& contact,
                      const blunted::Vector3& surfaceVelocity,
                      const ContactMaterial& material) {
    assert(ball.Mass() > 0.0f);
    assert(ball.Radius() > 0.0f);
    assert(ball.InertiaFactor() > 0.0f);
    assert(contact.normal.GetLength() > 1e-6f);

    football::model::BallState next = ball.State();
    const blunted::Vector3 normal = contact.normal.GetNormalized();
    const blunted::Vector3 leverArm = normal * -ball.Radius();
    const blunted::Vector3 contactVelocity =
        next.velocity - surfaceVelocity +
        next.angularVelocity.GetCrossProduct(leverArm);
    const float normalSpeed = contactVelocity.GetDotProduct(normal);
    if (normalSpeed >= 0.0f) return;

    const float normalImpulse =
        -(1.0f + material.restitution) * ball.Mass() * normalSpeed;
    const blunted::Vector3 normalImpulseVector = normal * normalImpulse;
    const blunted::Vector3 tangentialVelocity =
        contactVelocity - normal * normalSpeed;
    const float tangentialSpeed = tangentialVelocity.GetLength();

    blunted::Vector3 tangentialImpulse(0.0f, 0.0f, 0.0f);
    constexpr float kSpeedEpsilon = 1e-6f;
    if (tangentialSpeed > kSpeedEpsilon) {
      const float stickFactor =
          ball.InertiaFactor() / (1.0f + ball.InertiaFactor());
      const float stickImpulse =
          stickFactor * ball.Mass() * tangentialSpeed;
      const float frictionLimit = material.friction * std::fabs(normalImpulse);
      const float magnitude = std::min(stickImpulse, frictionLimit);
      tangentialImpulse =
          tangentialVelocity * (-magnitude / tangentialSpeed);
    }

    next.velocity += (normalImpulseVector + tangentialImpulse) / ball.Mass();
    const float inertia = ball.InertiaFactor() * ball.Mass() *
                          ball.Radius() * ball.Radius();
    next.angularVelocity += leverArm.GetCrossProduct(tangentialImpulse) / inertia;
    ball.SetState(next);
  }
};

}  // namespace football::contact

#endif  // FOOTBALL_CORE_CONTACT_BALL_CONTACT_RESOLVER_HPP

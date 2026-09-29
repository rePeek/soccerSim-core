#ifndef _HPP_CORE_CONTACT_BALL_CONTACT_RESOLVER
#define _HPP_CORE_CONTACT_BALL_CONTACT_RESOLVER

#include <algorithm>
#include <cassert>
#include <cmath>

#include "core/contact/ball_contact.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/state/ball_state.hpp"

namespace football::contact {

// Impulse response for a rigid sphere (7G-7c), with the inertia factor read
// from BallProfile so solid/spherical-shell/calibrated moments share the
// same math.
//
//   r      = -normal * radius                     (lever arm)
//   vc     = v + omega x r                        (contact-point velocity)
//   vn     = dot(vc, n)                           (normal approach speed)
//   Jn     = -(1 + e) m vn                        (normal impulse)
//   Jstick = (k / (1 + k)) m |vt|                 (I = k m R^2)
//   Jt     = min(Jstick, mu Jn)                   (Coulomb friction)
//   v     += (Jn n + Jt) / m
//   omega += (r x Jt) / I
//
// This is a pure function of geometry and material: it mutates only the given
// BallState and never touches Match, Humanoid or any gameplay bias. It is not
// wired into production yet (7G-7d moves the ground contact onto it).
struct BallContactResolver {
  static void Resolve(BallState &state,
                      const football::domain::BallProfile &ball,
                      const BallContact &contact,
                      const ContactMaterial &material) {
    DO_VALIDATION;
    assert(ball.mass > 0.0f);
    assert(ball.radius > 0.0f);
    assert(ball.inertiaFactor > 0.0f);
    // A contact without a usable normal is invalid: fail loudly instead of
    // silently turning it into a ground bounce.
    assert(contact.normal.GetLength() > 1e-6f);
    const blunted::Vector3 normal = contact.normal.GetNormalized();

    // The physics lever arm is the ball radius along the contact normal.
    // contact.point is used only for events/debug/penetration correction.
    const blunted::Vector3 leverArm = normal * -ball.radius;

    const blunted::Vector3 contactVelocity =
        state.velocity + state.angularVelocity.GetCrossProduct(leverArm);
    const float normalSpeed = contactVelocity.GetDotProduct(normal);

    // A separating contact needs no impulse; applying one would pull the ball
    // back onto the surface.
    if (normalSpeed >= 0.0f) return;

    const float normalImpulse =
        -(1.0f + material.restitution) * ball.mass * normalSpeed;
    const blunted::Vector3 normalImpulseVec = normal * normalImpulse;

    // Tangential (sliding) component of the contact-point velocity.
    const blunted::Vector3 tangentialVelocity =
        contactVelocity - normal * normalSpeed;
    const float tangentialSpeed = tangentialVelocity.GetLength();

    constexpr float kSpeedEpsilon = 1e-6f;
    blunted::Vector3 tangentialImpulse(0.0f, 0.0f, 0.0f);
    if (tangentialSpeed > kSpeedEpsilon) {
      const float stickFactor =
          ball.inertiaFactor / (1.0f + ball.inertiaFactor);
      const float stickImpulse = stickFactor * ball.mass * tangentialSpeed;
      const float frictionLimit =
          material.friction * std::fabs(normalImpulse);
      const float tangentialMagnitude = std::min(stickImpulse, frictionLimit);
      tangentialImpulse =
          tangentialVelocity * (-tangentialMagnitude / tangentialSpeed);
    }

    state.velocity += (normalImpulseVec + tangentialImpulse) / ball.mass;

    const float inertia =
        ball.inertiaFactor * ball.mass * ball.radius * ball.radius;
    state.angularVelocity +=
        leverArm.GetCrossProduct(tangentialImpulse) / inertia;
  }
};

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_BALL_CONTACT_RESOLVER
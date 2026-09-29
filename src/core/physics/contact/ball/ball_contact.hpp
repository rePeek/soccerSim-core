#ifndef _HPP_CORE_CONTACT_BALL_CONTACT
#define _HPP_CORE_CONTACT_BALL_CONTACT

#include "core/math/vector3.hpp"

namespace football_sim::contact {

// Ball-centric contact fact (7G-7c). Same frozen semantics as Contact: the
// normal points away from the colliding surface into the ball.
//
// `point` is the world-space geometric contact point reported by the
// detector (the ground plane / post / crossbar / player collider surface).
// It is NOT used to derive the sphere's impulse lever arm -- that is always
// -normal * radius -- so a detector that reports an obstacle-surface point
// cannot silently corrupt the spin response.
struct BallContact {
  football_sim::math::Vector3 point = football_sim::math::Vector3(0);
  football_sim::math::Vector3 normal = football_sim::math::Vector3(0, 0, 0);  // surface -> ball
  float penetration = 0.0f;  // reserved; the impulse solve is velocity-based

  // Seconds from the beginning of the current physics step, in [0, dt].
  // Discrete detectors report 0.0f (contact at the current instant); swept
  // detectors report the time of first impact. Never a [0,1] fraction or ms.
  float timeWithinTick = 0.0f;
};

// Material response, deliberately separate from the geometric contact.
struct ContactMaterial {
  float restitution = 0.62f;  // 0 = fully inelastic, 1 = elastic
  float friction = 0.4f;      // Coulomb friction coefficient (NOT legacy 0.04)
};

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_BALL_CONTACT
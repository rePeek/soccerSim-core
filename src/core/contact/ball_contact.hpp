#ifndef _HPP_CORE_CONTACT_BALL_CONTACT
#define _HPP_CORE_CONTACT_BALL_CONTACT

#include "foundation/math/vector3.hpp"

namespace football::contact {

// Ball-centric contact fact (7G-7c). Same frozen semantics as Contact: the
// normal points away from the colliding surface into the ball.
//
// `point` is the ball-surface contact point used for events, debug drawing
// and position/penetration correction. The impulse resolver deliberately
// does NOT derive its lever arm from `point`: for a sphere the physics lever
// arm is always -normal * radius, so a detector that reports an
// obstacle-surface point cannot silently corrupt the spin response.
struct BallContact {
  blunted::Vector3 point = blunted::Vector3(0);
  blunted::Vector3 normal = blunted::Vector3(0, 0, 0);  // surface -> ball
  float penetration = 0.0f;  // reserved; the impulse solve is velocity-based
};

// Material response, deliberately separate from the geometric contact.
struct ContactMaterial {
  float restitution = 0.62f;  // 0 = fully inelastic, 1 = elastic
  float friction = 0.04f;     // Coulomb friction coefficient
};

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_BALL_CONTACT
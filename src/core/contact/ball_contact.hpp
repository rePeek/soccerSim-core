#ifndef _HPP_CORE_CONTACT_BALL_CONTACT
#define _HPP_CORE_CONTACT_BALL_CONTACT

#include "foundation/math/vector3.hpp"

namespace football::contact {

// Ball-centric contact fact (7G-7c). Same frozen semantics as Contact: the
// point sits on the ball surface and the normal points away from the colliding
// surface into the ball, so the contact lever arm is
//
//   r = point - ballCentre
//
// and r is (anti-)parallel to the normal for a spherical contact. This is the
// input the impulse resolver needs to couple translation and spin.
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
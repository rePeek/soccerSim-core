#ifndef _HPP_CORE_CONTACT_CAPSULE_COLLIDER
#define _HPP_CORE_CONTACT_CAPSULE_COLLIDER

#include "foundation/math/vector3.hpp"

namespace football_sim::contact {

// A swept sphere: the set of points within `radius` of the segment
// [tipA, tipB]. Goal posts, the crossbar and the player body can all be built
// from these primitives.
struct CapsuleCollider {
  football_sim::math::Vector3 tipA = football_sim::math::Vector3(0);
  football_sim::math::Vector3 tipB = football_sim::math::Vector3(0);
  float radius = 0.0f;
};

// Closest point on segment [a, b] to p (the capsule spine point seen by a
// sphere centre).
inline football_sim::math::Vector3 ClosestPointOnSegment(const football_sim::math::Vector3 &p,
                                              const football_sim::math::Vector3 &a,
                                              const football_sim::math::Vector3 &b) {
  const football_sim::math::Vector3 ab = b - a;
  const float lengthSq = ab.GetDotProduct(ab);
  if (lengthSq <= 0.0f) return a;
  float t = (p - a).GetDotProduct(ab) / lengthSq;
  t = football_sim::math::clamp(t, 0.0f, 1.0f);
  return a + ab * t;
}

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_CAPSULE_COLLIDER
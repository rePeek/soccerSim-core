#ifndef _HPP_CORE_CONTACT_CAPSULE_COLLIDER
#define _HPP_CORE_CONTACT_CAPSULE_COLLIDER

#include "foundation/math/vector3.hpp"

namespace football::contact {

// A swept sphere: the set of points within `radius` of the segment
// [tipA, tipB]. Goal posts, the crossbar and the player body can all be built
// from these primitives.
struct CapsuleCollider {
  blunted::Vector3 tipA = blunted::Vector3(0);
  blunted::Vector3 tipB = blunted::Vector3(0);
  float radius = 0.0f;
};

// Closest point on segment [a, b] to p (the capsule spine point seen by a
// sphere centre).
inline blunted::Vector3 ClosestPointOnSegment(const blunted::Vector3 &p,
                                              const blunted::Vector3 &a,
                                              const blunted::Vector3 &b) {
  const blunted::Vector3 ab = b - a;
  const float lengthSq = ab.GetDotProduct(ab);
  if (lengthSq <= 0.0f) return a;
  float t = (p - a).GetDotProduct(ab) / lengthSq;
  t = blunted::clamp(t, 0.0f, 1.0f);
  return a + ab * t;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_CAPSULE_COLLIDER
#ifndef _HPP_CORE_CONTACT_CIRCLE_CONTACT
#define _HPP_CORE_CONTACT_CIRCLE_CONTACT

#include <optional>

#include "core/physics/contact/geometry/circle_collider.hpp"
#include "core/physics/contact/geometry/contact.hpp"

namespace football_sim::contact {

// Planar circle-circle detection, the primitive behind player-player ground
// contact. Pure geometry: no PlayerBase, no Match, no stats, no mutation.
//
// Frozen semantics (see contact.hpp):
//   normal       A -> B
//   penetration  rA + rB - distance
//   separated    nullopt
//   touching     Contact(penetration = 0)
//   overlap      Contact(penetration > 0)
//   coincident center -> deterministic fallback normal (+x)
inline std::optional<Contact> DetectContact(const CircleCollider &a,
                                            const CircleCollider &b) {

  const football_sim::math::Vector3 fromAtoB = (b.center - a.center).Get2D();
  const float distance = fromAtoB.GetLength();
  const float sumRadii = a.radius + b.radius;

  // Strict frozen semantics: separated means distance > sumRadii. There is no
  // slop here; tolerance belongs to the solver, not to contact geometry.
  if (distance > sumRadii) return std::nullopt;

  // Coincident centers have no unique normal; +x is an arbitrary but
  // deterministic tie-breaker. It is not claimed to be mirror-symmetric.
  Contact contact;
  contact.normal = fromAtoB.GetNormalized(football_sim::math::Vector3(1.0f, 0.0f, 0.0f));
  contact.normal.coords[2] = 0.0f;
  contact.penetration = sumRadii - distance;

  // Midpoint of the overlapping band along the contact line. penetration == 0
  // yields exactly a.center + normal * a.radius == b.center - normal * b.radius.
  contact.point =
      a.center + contact.normal * (a.radius - contact.penetration * 0.5f);
  contact.point.coords[2] = 0.0f;
  return contact;
}

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_CIRCLE_CONTACT
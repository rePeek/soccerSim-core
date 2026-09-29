#ifndef _HPP_CORE_CONTACT_CIRCLE_CONTACT
#define _HPP_CORE_CONTACT_CIRCLE_CONTACT

#include <algorithm>
#include <optional>

#include "core/contact/circle_collider.hpp"
#include "core/contact/contact.hpp"

namespace football::contact {

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
  DO_VALIDATION;
  const blunted::Vector3 fromAtoB = (b.center - a.center).Get2D();
  const float distance = fromAtoB.GetLength();
  const float sumRadii = a.radius + b.radius;

  // A hair of contact slop keeps the exact "touching" boundary robust to float
  // rounding. It can only ever report a contact for a marginally-separated
  // pair (penetration clamped to 0); it never invents an overlap.
  constexpr float kContactEpsilon = 1e-5f;
  if (distance > sumRadii + kContactEpsilon) return std::nullopt;

  // Coincident centers would make the normal undefined. Fall back to +x so the
  // result is deterministic across runs, replays and mirrored play.
  Contact contact;
  contact.normal = fromAtoB.GetNormalized(blunted::Vector3(1.0f, 0.0f, 0.0f));
  contact.normal.coords[2] = 0.0f;
  contact.penetration = std::max(0.0f, sumRadii - distance);

  // Midpoint of the overlapping band along the contact line. penetration == 0
  // yields exactly a.center + normal * a.radius == b.center - normal * b.radius.
  contact.point =
      a.center + contact.normal * (a.radius - contact.penetration * 0.5f);
  contact.point.coords[2] = 0.0f;
  return contact;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_CIRCLE_CONTACT
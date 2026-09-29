#ifndef _HPP_CORE_CONTACT_SPHERE_CAPSULE_CONTACT
#define _HPP_CORE_CONTACT_SPHERE_CAPSULE_CONTACT

#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/contact/capsule_collider.hpp"

namespace football::contact {

// Sphere vs capsule contact (7G-7e). Frozen semantics mirror the other
// detectors: the normal points from the capsule surface into the sphere, the
// point is on the capsule surface, and penetration = rSphere + rCapsule -
// distance. A sphere centred exactly on the capsule spine has no unique
// normal and falls back to a deterministic +x tie-breaker.
inline std::optional<BallContact> DetectSphereCapsuleContact(
    const blunted::Vector3 &center, float sphereRadius,
    const CapsuleCollider &capsule) {
  DO_VALIDATION;
  const blunted::Vector3 spinePoint =
      ClosestPointOnSegment(center, capsule.tipA, capsule.tipB);
  const blunted::Vector3 fromCapsuleToCenter = center - spinePoint;
  const float distance = fromCapsuleToCenter.GetLength();
  const float sumRadii = sphereRadius + capsule.radius;
  if (distance > sumRadii) return std::nullopt;

  BallContact contact;
  contact.normal =
      fromCapsuleToCenter.GetNormalized(blunted::Vector3(1.0f, 0.0f, 0.0f));
  contact.point = spinePoint + contact.normal * capsule.radius;
  contact.penetration = sumRadii - distance;
  return contact;
}

}  // namespace football::contact

#endif  // _HPP_CORE_CONTACT_SPHERE_CAPSULE_CONTACT
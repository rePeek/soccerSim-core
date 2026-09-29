#ifndef _HPP_CORE_CONTACT_SWEEP_SPHERE_CAPSULE
#define _HPP_CORE_CONTACT_SWEEP_SPHERE_CAPSULE

#include <cmath>
#include <optional>

#include "core/contact/ball_contact.hpp"
#include "core/contact/capsule_collider.hpp"

namespace football_sim::contact {

// Internal: smallest root t in [0,1] of a t^2 + b t + c = 0.
inline bool SmallestRootInUnitInterval(float a, float b, float c, float &t) {
  constexpr float kEpsilon = 1e-9f;
  if (std::fabs(a) < kEpsilon) {
    if (std::fabs(b) < kEpsilon) return false;
    const float r = -c / b;
    if (r >= 0.0f && r <= 1.0f) {
      t = r;
      return true;
    }
    return false;
  }
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) {
    // Near-tangent: a tiny negative discriminant (pure float rounding at a
    // grazing contact) is a single root, not a miss.
    const float tangentTolerance = 1e-4f * std::max(1.0f, b * b);
    if (discriminant < -tangentTolerance) return false;
    const float root = -b * (0.5f / a);
    if (root >= 0.0f && root <= 1.0f) {
      t = root;
      return true;
    }
    return false;
  }
  const float sqrtD = std::sqrt(discriminant);
  const float inv2a = 0.5f / a;
  const float t1 = (-b - sqrtD) * inv2a;
  const float t2 = (-b + sqrtD) * inv2a;
  if (t1 >= 0.0f && t1 <= 1.0f) {
    t = t1;
    return true;
  }
  if (t2 >= 0.0f && t2 <= 1.0f) {
    t = t2;
    return true;
  }
  return false;
}

// 7G-8-a: swept sphere vs static capsule, pure geometry.
//
// Contract:
//   timeWithinTick  seconds into this physics step, in [0, dt]
//   normal          obstacle -> sphere, normalized, taken at the time of impact
//   point           obstacle-surface point at the time of impact
//   penetration     0 for a normal swept hit; positive only when the sphere
//                   already overlapped at the start of the sweep (then
//                   timeWithinTick == 0)
//
// The sweep is the linear chord start -> end (CCD v1: a 10 ms tick is
// approximated as straight-line motion). This kernel knows nothing about
// velocity, forces or the resolver -- it only answers "when does the moving
// point enter the expanded capsule (R = sphereRadius + capsule.radius)".
inline std::optional<BallContact> SweepSphereCapsule(
    const football_sim::math::Vector3 &start, const football_sim::math::Vector3 &end,
    float sphereRadius, const CapsuleCollider &capsule, float dt) {
  DO_VALIDATION;
  const float expandedRadius = sphereRadius + capsule.radius;
  const football_sim::math::Vector3 axis = capsule.tipB - capsule.tipA;
  const float axisLengthSq = axis.GetSquaredLength();
  const football_sim::math::Vector3 motion = end - start;

  // Already touching or overlapping at the start of the step: report the
  // current instant using the discrete geometry.
  const football_sim::math::Vector3 spineStart =
      ClosestPointOnSegment(start, capsule.tipA, capsule.tipB);
  const float startDistance = (start - spineStart).GetLength();
  if (startDistance <= expandedRadius) {
    BallContact contact;
    contact.normal =
        (start - spineStart).GetNormalized(football_sim::math::Vector3(1.0f, 0.0f, 0.0f));
    contact.point = spineStart + contact.normal * capsule.radius;
    contact.penetration = expandedRadius - startDistance;
    contact.timeWithinTick = 0.0f;
    return contact;
  }

  float bestT = 2.0f;  // > 1 means "no hit"
  int bestKind = -1;   // 0 body, 1 cap A, 2 cap B

  // Cap A: swept point vs sphere at tipA. Valid only when the hit point is
  // beyond the A end (closest segment point is tipA).
  {
    const football_sim::math::Vector3 m = start - capsule.tipA;
    const float a = motion.GetSquaredLength();
    const float b = 2.0f * m.GetDotProduct(motion);
    const float c = m.GetSquaredLength() - expandedRadius * expandedRadius;
    float t;
    if (SmallestRootInUnitInterval(a, b, c, t) && t < bestT) {
      const football_sim::math::Vector3 center = start + motion * t;
      if ((center - capsule.tipA).GetDotProduct(axis) <= 0.0f) {
        bestT = t;
        bestKind = 1;
      }
    }
  }

  // Capsule body: swept point vs the infinite cylinder around the axis,
  // valid only while the hit's projection stays inside the segment.
  if (axisLengthSq > 1e-12f) {
    const football_sim::math::Vector3 m = start - capsule.tipA;
    const float projM = m.GetDotProduct(axis) / axisLengthSq;
    const float projD = motion.GetDotProduct(axis) / axisLengthSq;
    const football_sim::math::Vector3 f = m - axis * projM;  // perpendicular of start
    const football_sim::math::Vector3 g = motion - axis * projD;  // perpendicular of motion
    const float a = g.GetSquaredLength();
    const float b = 2.0f * f.GetDotProduct(g);
    const float c = f.GetSquaredLength() - expandedRadius * expandedRadius;
    float t;
    if (SmallestRootInUnitInterval(a, b, c, t) && t < bestT) {
      const football_sim::math::Vector3 center = start + motion * t;
      const float proj = (center - capsule.tipA).GetDotProduct(axis);
      if (proj >= 0.0f && proj <= axisLengthSq) {
        bestT = t;
        bestKind = 0;
      }
    }
  }

  // Cap B: swept point vs sphere at tipB. Valid only when the hit point is
  // beyond the B end.
  {
    const football_sim::math::Vector3 m = start - capsule.tipB;
    const float a = motion.GetSquaredLength();
    const float b = 2.0f * m.GetDotProduct(motion);
    const float c = m.GetSquaredLength() - expandedRadius * expandedRadius;
    float t;
    if (SmallestRootInUnitInterval(a, b, c, t) && t < bestT) {
      const football_sim::math::Vector3 center = start + motion * t;
      if ((center - capsule.tipA).GetDotProduct(axis) >= axisLengthSq) {
        bestT = t;
        bestKind = 2;
      }
    }
  }

  if (bestKind < 0) return std::nullopt;

  const float u = bestT;
  const football_sim::math::Vector3 centerAtHit = start + motion * u;
  const football_sim::math::Vector3 spinePoint =
      ClosestPointOnSegment(centerAtHit, capsule.tipA, capsule.tipB);

  BallContact contact;
  contact.normal =
      (centerAtHit - spinePoint).GetNormalized(football_sim::math::Vector3(1.0f, 0.0f, 0.0f));
  contact.point = spinePoint + contact.normal * capsule.radius;
  contact.penetration = 0.0f;
  contact.timeWithinTick = u * dt;
  return contact;
}

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_SWEEP_SPHERE_CAPSULE
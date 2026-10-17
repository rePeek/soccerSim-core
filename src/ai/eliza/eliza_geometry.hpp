#ifndef FOOTBALL_AI_ELIZA_GEOMETRY_HPP
#define FOOTBALL_AI_ELIZA_GEOMETRY_HPP

#include <cmath>

#include "football/foundation/math/scalar.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ai::eliza {

// Pure 2D geometry helpers. They replace the legacy `Line` queries with value
// inputs, so no Match/Player/MentalImage is needed.
using football::math::Vector3;

// Projection parameter of p on the line a-b, unclamped. Mirrors the legacy
// Line::GetDistanceToPoint parameter u.
inline float SegmentParameter(const Vector3& a, const Vector3& b, const Vector3& p) {
  const Vector3 ab = (b - a).Get2D();
  const float denom = ab.GetSquaredLength();
  if (denom <= 1e-8f) return 0.0f;
  return (p - a).Get2D().GetDotProduct(ab) / denom;
}

// Distance from p to the line a-b plus the unclamped projection parameter.
inline float DistanceToPoint(const Vector3& a, const Vector3& b, const Vector3& p,
                             float& u) {
  u = SegmentParameter(a, b, p);
  const float clamped = football::math::clamp(u, 0.0f, 1.0f);
  const Vector3 closest = a + (b - a) * clamped;
  return (p - closest).Get2D().GetLength();
}

// 2D intersection of the infinite lines a0-a1 and b0-b1. `u` is the parameter
// along a0-a1. Parallel lines return a0 with u = 0.
inline Vector3 Intersection(const Vector3& a0, const Vector3& a1,
                            const Vector3& b0, const Vector3& b1, float& u) {
  const Vector3 r = (a1 - a0).Get2D();
  const Vector3 s = (b1 - b0).Get2D();
  const float denom = r.coords[0] * s.coords[1] - r.coords[1] * s.coords[0];
  if (std::fabs(denom) <= 1e-8f) {
    u = 0.0f;
    return a0;
  }
  const Vector3 qp = (b0 - a0).Get2D();
  u = (qp.coords[0] * s.coords[1] - qp.coords[1] * s.coords[0]) / denom;
  return a0 + r * u;
}

// Direction safety: a zero-length vector falls back to `fallback`.
inline Vector3 SafeNormalized(const Vector3& value, const Vector3& fallback) {
  return value.GetNormalized(fallback);
}

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_GEOMETRY_HPP

#ifndef FOOTBALL_COLLISION_PENETRATION2D_HPP
#define FOOTBALL_COLLISION_PENETRATION2D_HPP

#include "football/foundation/math/vector3.hpp"

namespace football::collision {

// Planar penetration tests for the horizontal volumes players occupy.
//
// They are deliberately separate from Overlap(): the legacy player contest,
// tackle and body tests treat *exact tangency as no contact* (strict `<`), while
// Overlap() counts touching as overlap (`<=`). Switching the comparison would
// silently change which shoulder-to-shoulder and edge-of-reach duels register, so
// these keep the strict semantics. Everything is evaluated on the pitch plane;
// any z component is dropped by the projection, exactly as the callers did.

// Two horizontal circles. Strict: touching is not penetration.
inline bool PenetratesCircleCircle2D(const football::math::Vector3& center_a,
                                    float radius_a,
                                    const football::math::Vector3& center_b,
                                    float radius_b) {
  return (center_a - center_b).Get2D().GetLength() < radius_a + radius_b;
}

// Horizontal capsule [origin, origin + axis * length] against a horizontal
// circle. `axis` is expected to be unit length and planar. A zero length reduces
// the capsule to a circle. Strict: touching is not penetration.
inline bool PenetratesCapsuleCircle2D(const football::math::Vector3& origin,
                                     const football::math::Vector3& axis,
                                     float length, float radius,
                                     const football::math::Vector3& circle_center,
                                     float circle_radius) {
  const football::math::Vector3 to_center = (circle_center - origin).Get2D();
  const float along = football::math::clamp(to_center.GetDotProduct(axis), 0.0f, length);
  const football::math::Vector3 closest = origin + axis * along;
  return (circle_center - closest).Get2D().GetLength() < radius + circle_radius;
}

}  // namespace football::collision

#endif  // FOOTBALL_COLLISION_PENETRATION2D_HPP

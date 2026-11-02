#include "football/animation/touch_geometry.hpp"

namespace football::animation {

using football::math::Vector3;

float SegmentProjectionParameter(const Vector3& a, const Vector3& b,
                                 const Vector3& point) {
  if (a == b) return 0.0f;
  const float lineDistance = (b - a).GetLength();
  if (lineDistance < 0.000001f) return 0.0f;
  return ((point.coords[0] - a.coords[0]) * (b.coords[0] - a.coords[0]) +
          (point.coords[1] - a.coords[1]) * (b.coords[1] - a.coords[1])) /
         (lineDistance * lineDistance);
}

}  // namespace football::animation

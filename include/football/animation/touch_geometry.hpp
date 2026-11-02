#pragma once

#include "football/foundation/math/vector3.hpp"

namespace football::animation {

// Unclamped segment parameter. Preserves the historical 2D numerator / 3D
// length formula; callers decide how to interpret or clamp the result.
float SegmentProjectionParameter(const football::math::Vector3& a,
                                 const football::math::Vector3& b,
                                 const football::math::Vector3& point);

}  // namespace football::animation

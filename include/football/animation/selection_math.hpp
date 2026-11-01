#pragma once

#include "football/animation/types.hpp"

inline football::math::radian FixAngle(football::math::radian angle) {
  // Convert engine angle into football angle (different base orientation:
  // 'down' on y instead of 'right' on x).
  angle += 0.5f * football::math::pi;
  return football::math::ModulateIntoRange(-football::math::pi, football::math::pi, angle);
}

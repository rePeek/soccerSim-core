// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _HPP_ANIMATION_SELECTION_MATH
#define _HPP_ANIMATION_SELECTION_MATH

#include "football/animation/types.hpp"

inline football::math::radian FixAngle(football::math::radian angle) {
  // Convert engine angle into football angle (different base orientation:
  // 'down' on y instead of 'right' on x).
  angle += 0.5f * football::math::pi;
  return football::math::ModulateIntoRange(-football::math::pi, football::math::pi, angle);
}

#endif

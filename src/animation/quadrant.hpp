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

#ifndef _HPP_ANIMATION_QUADRANT
#define _HPP_ANIMATION_QUADRANT

#include "foundation/math/vector3.hpp"
#include "animation/types.hpp"

// Quantized outgoing movement (velocity plus angle). Kept as a pure value type
// so baked runtime selection stays independent of the simulation runtime.
struct Quadrant {
  int id = 0;
  blunted::Vector3 position;
  e_Velocity velocity;
  blunted::radian angle;
};

#endif

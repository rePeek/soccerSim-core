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

#ifndef _HPP_CORE_STATE_BALL_STATE
#define _HPP_CORE_STATE_BALL_STATE

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

// Authoritative, behavior-free state of the ball at one simulation tick.
//
// Plain simulation data only: no pointers to Match/GameEnv/controllers, no
// prediction caches. Those stay in legacy Ball (Phase 4).
struct BallState {
  blunted::Vector3 position;
  blunted::Vector3 momentum;        // meters / sec
  blunted::Quaternion rotation_ms;  // radians per second per axis
  blunted::Quaternion orientation;  // accumulated orientation
};

#endif  // _HPP_CORE_STATE_BALL_STATE
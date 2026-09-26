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

#ifndef _HPP_PLAYER_KINEMATICS
#define _HPP_PLAYER_KINEMATICS

#include "../../defines.hpp"

// Explicit movement state for a player. During the initial shadow phase it is
// synchronized from Humanoid; PlayerKinematics will later produce it directly
// without animation root motion.
struct PlayerKinematicState {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(0, -1, 0);
  float speed = 0.0f;

  void Mirror() {
    position.Mirror();
    velocity.Mirror();
    facing.Mirror();
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(velocity);
    state->process(facing);
    state->process(speed);
  }
};

#endif

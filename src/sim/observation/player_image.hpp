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

#ifndef FOOTBALL_SIM_OBSERVATION_PLAYER_IMAGE_HPP
#define FOOTBALL_SIM_OBSERVATION_PLAYER_IMAGE_HPP

#include "foundation/math/vector3.hpp"
#include "model/football_types.hpp"
#include "sim/animation/types.hpp"

using namespace blunted;

class Player;

// Internal execution-history projection; unlike WorldState, it retains actors.
struct PlayerImage {
  Vector3 position;
  Vector3 directionVec;
  Vector3 movement;
  Player *player;
  e_Velocity velocity = e_Velocity_Idle;
  e_PlayerRole role;
  void Mirror() {
    position.Mirror();
    directionVec.Mirror();
    movement.Mirror();
  }
};

struct PlayerImagePosition {
  PlayerImagePosition(const Vector3& position, const Vector3& movement, e_PlayerRole player_role) : position(position), movement(movement), player_role(player_role) { }
  Vector3 position;
  Vector3 movement;
  e_PlayerRole player_role;
};

#endif  // FOOTBALL_SIM_OBSERVATION_PLAYER_IMAGE_HPP

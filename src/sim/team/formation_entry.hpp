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

#ifndef FOOTBALL_SIM_TEAM_FORMATION_ENTRY_HPP
#define FOOTBALL_SIM_TEAM_FORMATION_ENTRY_HPP

#include <string>

#include "foundation/math/vector3.hpp"
#include "model/football_types.hpp"

using namespace blunted;

const int playerNum = 11;
const float FORMATION_Y_SCALE = -2.36f;

// Legacy role-adapted runtime representation. Initial domain declarations
struct FormationEntry {
  FormationEntry() { }
  // Constructor accepts environment coordinates.
  FormationEntry(float x, float y, e_PlayerRole role, bool lazy,
                 bool controllable)
      : position(x, y * FORMATION_Y_SCALE, 0),
        start_position(x, y * FORMATION_Y_SCALE, 0),
        role(role),
        lazy(lazy),
        controllable(controllable) {
  }
  bool operator == (const FormationEntry& f) const {
    return role == f.role &&
        lazy == f.lazy &&
        position == f.position &&
        controllable == f.controllable;
  }
  Vector3 position_env() {
    return Vector3(position.coords[0],
                   position.coords[1] / FORMATION_Y_SCALE,
                   position.coords[2]);
  }
  Vector3 position; // adapted to player role (combination of databasePosition and hardcoded role position)
  Vector3 start_position;
  e_PlayerRole role = e_PlayerRole_GK;
  bool lazy = false; // Computer doesn't perform any actions for lazy player.
  // Can be controlled by the player?
  bool controllable = true;
};

e_PlayerRole GetRoleFromString(const std::string &roleString);

#endif  // FOOTBALL_SIM_TEAM_FORMATION_ENTRY_HPP

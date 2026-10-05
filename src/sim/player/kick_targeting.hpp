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


#ifndef FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP
#define FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP

#include "sim/gamedefines.hpp"

class Player;

namespace football::sim::mechanics {

// Pass/shot direction and power used both when queuing and executing kicks.
void GetPass(Player *player, e_FunctionType passType,
             const Vector3 &inputDirection, float inputPower,
             float autoDirectionBias, float autoPowerBias,
             Vector3 &resultingDirection, float &resultingPower,
             Player *&targetPlayer, Player *forcedTargetPlayer = 0);
// Rough shot direction for animation selection; refined during execution.
Vector3 GetShotDirection(Player *player, const Vector3 &inputDirection,
                         float autoDirectionBias = 1.0f);

}  // namespace football::sim::mechanics

#endif  // FOOTBALL_SIM_PLAYER_KICK_TARGETING_HPP

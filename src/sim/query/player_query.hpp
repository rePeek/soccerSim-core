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


#ifndef FOOTBALL_SIM_QUERY_PLAYER_QUERY_HPP
#define FOOTBALL_SIM_QUERY_PLAYER_QUERY_HPP

#include <vector>
#include "foundation/math/vector3.hpp"

#include "foundation/time/tick.hpp"
class Team;
class Player;
namespace football::ball { class Ball; }
class MentalImage;

namespace football::sim::query {

// Estimates and selections over the runtime world; no tactical decision owner.
float CalculateFreeSpace(Tick now, const MentalImage *mentalImage,
                         int teamID, const blunted::Vector3 &focusPos,
                         float safeDistance = 8.0, float futureTime_sec = 0.3);
bool HasPossession(football::ball::Ball *ball, Player *player);

// Equal distances retain roster order; GetClosestPlayers appends to result.
Player *GetClosestPlayer(Team *team, const blunted::Vector3 &position,
                         Player *except = nullptr);
void GetClosestPlayers(Team *team, const blunted::Vector3 &position,
                       std::vector<Player*> &result, unsigned int playerCount = 3);

}  // namespace football::sim::query

#endif  // FOOTBALL_SIM_QUERY_PLAYER_QUERY_HPP

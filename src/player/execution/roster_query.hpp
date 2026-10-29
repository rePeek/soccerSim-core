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

#ifndef FOOTBALL_SIM_PLAYER_ROSTER_QUERY_HPP
#define FOOTBALL_SIM_PLAYER_ROSTER_QUERY_HPP

#include <span>
#include <vector>

#include "football/foundation/math/vector3.hpp"

class Player;

namespace football::sim::player {

// Spatial selection over a borrowed player set. The caller owns the roster and
// passes a stable view (for example Team::GetAllPlayers()); these helpers never
// know what a Team is. Equal distances retain input order, and
// FindClosestPlayers appends to `result`.
Player *FindClosestPlayer(std::span<Player *const> players,
                          const football::math::Vector3 &position,
                          const Player *excluded = nullptr);
void FindClosestPlayers(std::span<Player *const> players,
                        const football::math::Vector3 &position,
                        std::vector<Player*> &result,
                        unsigned int playerCount = 3);
// Distance to the nearest opponent in the borrowed view, or 0 for an empty
// roster. A world-state query, not Player state.
float ClosestOpponentDistance(std::span<Player *const> opponents,
                              const football::math::Vector3 &position);

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_ROSTER_QUERY_HPP

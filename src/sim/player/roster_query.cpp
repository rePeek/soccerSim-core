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

#include "sim/player/roster_query.hpp"

#include <map>

#include "sim/player/player.hpp"

namespace football::sim::player {

Player *FindClosestPlayer(std::span<Player *const> players,
                          const blunted::Vector3 &position,
                          const Player *excluded) {
  float closestDistance = 10000;
  Player *closestPlayer = nullptr;
  for (Player *player : players) {
    if (!player->IsActive() || player == excluded) continue;
    const float distance = (player->GetPosition() - position).GetLength();
    if (distance < closestDistance) {
      closestDistance = distance;
      closestPlayer = player;
    }
  }
  return closestPlayer;
}

void FindClosestPlayers(std::span<Player *const> players,
                        const blunted::Vector3 &position,
                        std::vector<Player *> &result, unsigned int playerCount) {
  // Equivalent keys preserve insertion/roster order, never PlayerId order.
  std::multimap<float, Player *> sorted;
  for (Player *player : players) {
    if (player->IsActive())
      sorted.emplace((player->GetPosition() - position).GetLength(), player);
  }
  auto iter = sorted.begin();
  for (unsigned int i = 0; i < playerCount && iter != sorted.end(); ++i, ++iter)
    result.push_back(iter->second);
}

}  // namespace football::sim::player

#include "sim/player/player_team_view.hpp"

#include "sim/player/player.hpp"

int ActivePlayerCount(std::span<Player* const> players) {
  int count = 0;
  for (Player* player : players) {
    if (player->IsActive()) ++count;
  }
  return count;
}

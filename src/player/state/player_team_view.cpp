#include "player/state/player_team_view.hpp"

#include "player/player.hpp"

int ActivePlayerCount(std::span<Player* const> players) {
  int count = 0;
  for (Player* player : players) {
    if (player->IsActive()) ++count;
  }
  return count;
}

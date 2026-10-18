#include "runtime/team.hpp"

#include <utility>
#include "player/player.hpp"

namespace football::sim {
Team::Team(TeamConfig config) : config_(std::move(config)) {}
Team::~Team() = default;
Player* Team::FindPlayer(football::player::PlayerId id) const {
  for (Player* player : Players()) if (player->GetID() == id) return player;
  return nullptr;
}
}  // namespace football::sim

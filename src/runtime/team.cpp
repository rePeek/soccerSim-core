#include "runtime/team.hpp"

#include <utility>
#include "player/player.hpp"

namespace football::sim {
Team::Team(TeamConfig config) : config_(std::move(config)),
    active_lineup_{config_.initial_lineup.starters} {
  match_state_.possession_amount = 1.0f;
  match_state_.fading_possession_amount = 1.0f;
}
Team::~Team() = default;
Player* Team::FindPlayer(football::player::PlayerId id) const {
  for (Player* player : Players()) if (player->GetID() == id) return player;
  return nullptr;
}
}  // namespace football::sim

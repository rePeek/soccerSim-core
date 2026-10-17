#include "event/touch_query.hpp"
#include "player/player.hpp"

namespace football::sim::event {
Player* LastTouchPlayer(const TouchState& touches, int team_id,
                        std::span<Player* const> players) {
  const auto id = touches.last_player_by_team[team_id];
  if (id == football::player::kInvalidPlayerId) return nullptr;
  for (Player* player : players)
    if (player->GetID() == id) return player;
  return nullptr;
}
float TeamTouchBias(const TouchState& touches, int team_id,
                    std::span<Player* const> players, int decay_ms, Tick now) {
  auto* player = LastTouchPlayer(touches, team_id, players);
  return player ? player->GetLastTouchBias(decay_ms, now) : 0;
}
} // namespace football::sim::event

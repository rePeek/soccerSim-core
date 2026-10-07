#include "sim/event/touch_query.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"

namespace football::sim::event {
Player* LastTouchPlayer(const TouchState& touches, const Team& team) {
  const auto id = touches.last_player_by_team[team.GetID()];
  if (id == football::model::kInvalidPlayerId) return nullptr;
  for (Player* player : team.GetAllPlayers())
    if (player->GetID() == id) return player;
  return nullptr;
}
float TeamTouchBias(const TouchState& touches, const Team& team, int decay_ms, Tick now) {
  auto* player = LastTouchPlayer(touches, team);
  return player ? player->GetLastTouchBias(decay_ms, now) : 0;
}
} // namespace football::sim::event

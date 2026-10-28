#ifndef FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP

#include <span>

#include "event/touch_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "player/contact/player_touch_query.hpp"

class Player;

namespace football::sim::event {
// Event/Runtime-facing forwarding path. The implementation is Player-owned
// (player/player_touch_query.*); the TouchState is a PlayerTouchFacts
// subobject, so the borrowed facts upcast without a copy.
inline Player* LastTouchPlayer(const TouchState& touches, int team_id,
                               std::span<Player* const> players) {
  return football::sim::player::LastTouchPlayer(touches, team_id, players);
}
inline float TeamTouchBias(const TouchState& touches, int team_id,
                           std::span<Player* const> players, int decay_ms,
                           Tick now) {
  return football::sim::player::TeamTouchBias(touches, team_id, players,
                                              decay_ms, now);
}
}  // namespace football::sim::event
#endif

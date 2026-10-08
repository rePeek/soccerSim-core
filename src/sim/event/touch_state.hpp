#ifndef FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP

#include <array>
#include "sim/event/touch_type.hpp"
#include "model/player.hpp"

namespace football::sim::event {

// Competition-wide touch values only; no actor/runtime owner or notification.
struct TouchState {
  int last_team = -1;
  std::array<int, e_TouchType_SIZE> last_team_by_type;
  std::array<football::model::PlayerId, 2> last_player_by_team;
  TouchState() { Reset(); }
  void Record(int team, football::model::PlayerId player, e_TouchType type) {
    last_player_by_team[team] = player;
    last_team_by_type[type] = team;
    last_team = team;
  }
  void Reset() {
    last_team_by_type.fill(-1);
    last_team = -1;
    last_player_by_team.fill(football::model::kInvalidPlayerId);
  }
};

} // namespace football::sim::event
#endif

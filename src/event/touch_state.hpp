#ifndef FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP

#include <array>
#include "event/touch_type.hpp"
#include "football/player/player_touch_facts.hpp"
#include "football/player/player_definition.hpp"

namespace football::sim::event {

// Competition-wide touch values only; no actor/runtime owner or notification.
// The Player-readable slice is the borrowed base subobject so an accepted
// touch landing inside the same Process() call is visible to the actor.
struct TouchState : football::player::PlayerTouchFacts {
  std::array<int, e_TouchType_SIZE> last_team_by_type;
  TouchState() { Reset(); }
  void Record(int team, football::player::PlayerId player, e_TouchType type) {
    last_player_by_team[team] = player;
    last_team_by_type[type] = team;
    last_team = team;
  }
  void Reset() {
    last_team_by_type.fill(-1);
    last_team = -1;
    last_player_by_team.fill(football::player::kInvalidPlayerId);
  }
};

} // namespace football::sim::event
#endif

#ifndef FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_STATE_HPP

#include <array>
#include "sim/ball/ball_touch.hpp"

namespace football::sim::event {

// Competition-wide touch values only; no actor/runtime owner or notification.
struct TouchState {
  int last_team = -1;
  std::array<int, e_TouchType_SIZE> last_team_by_type;
  TouchState() { Reset(); }
  void Record(int team, e_TouchType type) {
    last_team_by_type[type] = team;
    last_team = team;
  }
  void Reset() {
    last_team_by_type.fill(-1);
    last_team = -1;
  }
};

} // namespace football::sim::event
#endif

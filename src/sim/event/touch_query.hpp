#ifndef FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP

#include "sim/event/touch_state.hpp"
#include "foundation/time/tick.hpp"

class Player;
class Team;

namespace football::sim::event {
// Identity lookup includes inactive/sent-off entries, matching the old team record.
Player* LastTouchPlayer(const TouchState& touches, const Team& team);
float TeamTouchBias(const TouchState& touches, const Team& team, int decay_ms, Tick now);
} // namespace football::sim::event
#endif

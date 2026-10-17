#ifndef FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_QUERY_HPP

#include <span>

#include "event/touch_state.hpp"
#include "football/foundation/time/tick.hpp"

class Player;

namespace football::sim::event {
// Identity lookup includes inactive/sent-off entries, matching the old team
// record. The caller supplies the team id and its roster slice, so this query
// no longer needs a runtime Team object.
Player* LastTouchPlayer(const TouchState& touches, int team_id,
                        std::span<Player* const> players);
float TeamTouchBias(const TouchState& touches, int team_id,
                    std::span<Player* const> players, int decay_ms, Tick now);
} // namespace football::sim::event
#endif

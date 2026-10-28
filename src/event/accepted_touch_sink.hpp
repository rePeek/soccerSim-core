#ifndef FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP
#define FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP

// Compatibility path. The synchronous accepted-touch port is Player-owned now;
// Event/Runtime implement it to receive the confirmed value.
#include "player/contact/player_touch_event.hpp"

namespace football::sim {

using AcceptedTouchSink = football::sim::player::PlayerTouchSink;

}  // namespace football::sim

#endif  // FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_SINK_HPP

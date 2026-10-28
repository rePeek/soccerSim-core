#ifndef FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP
#define FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP

// Compatibility path. The confirmed-touch carrier is a Player-owned value now;
// Event consumes it. See player/player_touch_event.hpp.
#include "player/execution/player_touch_event.hpp"

namespace football::sim::event {

using AcceptedTouch = football::sim::player::PlayerTouchEvent;

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_ACCEPTED_TOUCH_HPP

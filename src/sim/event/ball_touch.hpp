#ifndef FOOTBALL_SIM_EVENT_BALL_TOUCH_HPP
#define FOOTBALL_SIM_EVENT_BALL_TOUCH_HPP

#include "sim/ball/ball_touch.hpp"
#include "sim/time/tick.hpp"

class Player;
class Team;

namespace football::sim {

// One synchronous publication; never a delayed physics/rules event queue.
struct BallTouchEvent {
  Tick now{};
  Player* player = nullptr;
  Team* team = nullptr;
  e_TouchType type = e_TouchType_None;
};

} // namespace football::sim
#endif

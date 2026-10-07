#ifndef FOOTBALL_SIM_BALL_BALL_TOUCH_EVENT_HPP
#define FOOTBALL_SIM_BALL_BALL_TOUCH_EVENT_HPP

#include "sim/ball/ball_touch.hpp"
#include "sim/time/tick.hpp"

class Player;
class Team;

namespace football::sim {

// One synchronous touch publication. Actors construct this and hand it to the
// sink; the sink owns all bookkeeping and rule notification.
struct BallTouchEvent {
  Tick now{};
  Player* player = nullptr;
  Team* team = nullptr;
  e_TouchType type = e_TouchType_None;
};

// Write-only port. Implementations must expose no world queries, so callers
// cannot turn a touch notification back into a service-locator lookup.
class BallTouchSink {
 public:
  virtual ~BallTouchSink() = default;
  virtual void OnBallTouched(const BallTouchEvent& event) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_BALL_BALL_TOUCH_EVENT_HPP

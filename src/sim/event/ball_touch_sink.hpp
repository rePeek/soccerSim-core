#ifndef FOOTBALL_SIM_EVENT_BALL_TOUCH_SINK_HPP
#define FOOTBALL_SIM_EVENT_BALL_TOUCH_SINK_HPP

#include "sim/event/ball_touch.hpp"

namespace football::sim {

// Write-only synchronous port. No world queries, actor-retained runtime pointer,
// phase-end buffering or implicit RNG draws.
class BallTouchSink {
 public:
  virtual ~BallTouchSink() = default;
  virtual void OnBallTouched(const BallTouchEvent& event) = 0;
};

} // namespace football::sim
#endif

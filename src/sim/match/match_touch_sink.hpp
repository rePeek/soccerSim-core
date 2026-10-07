#ifndef FOOTBALL_SIM_MATCH_MATCH_TOUCH_SINK_HPP
#define FOOTBALL_SIM_MATCH_MATCH_TOUCH_SINK_HPP

#include "sim/ball/ball_touch_event.hpp"

class Match;

namespace football::sim {

// Runtime composition for touch publication: writes the team/player touch facts
// and the competition touch ids, then notifies the referee synchronously. It
// holds the runtime owner but exposes only the write-only sink interface to
// actors.
class MatchTouchSink final : public BallTouchSink {
 public:
  explicit MatchTouchSink(Match& match) : match_(match) {}
  void OnBallTouched(const BallTouchEvent& event) override;

 private:
  Match& match_;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_MATCH_MATCH_TOUCH_SINK_HPP

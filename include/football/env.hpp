#pragma once

#include "football/ball/ball.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/history.hpp"
#include "football/pitch/pitch.hpp"

namespace football {

class Env {
 public:
  explicit Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               history::History history = {});
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

  void LoopMain(time::TickSpan tick_limit);

  const history::History& history() const { return history_; }

  time::Tick Ticks() const;

 private:
  void Step();

  pitch::Pitch pitch_;
  ball::Ball ball_;
  history::History history_;
  time::Tick tick_{0};
};

}

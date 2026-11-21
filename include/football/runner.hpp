#pragma once

#include <thread>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::runner {

class SingleRunner {
 public:
  SingleRunner(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               history::Recording recording = history::Recording::Enabled,
               time::TickSpan tick_limit = {});
  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  void Start();

  void Wait();

  const history::History& history() const;

  const Env& EnvironmentAfterJoin() const;

 private:
  Env env_;
  time::TickSpan tick_limit_{};
  bool started_ = false;
  std::jthread thread_;
};

}

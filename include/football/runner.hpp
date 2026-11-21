#pragma once

#include <stop_token>
#include <thread>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::runner {

class SingleRunner {
 public:
  SingleRunner(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               history::Recording recording = history::Recording::Enabled,
               time::TickSpan ticks_per_step = {});

  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  void Start();

  void Wait();

  void Stop();

  const history::History& history() const;

  const Env& EnvironmentAfterJoin() const;

 private:
  void Loop(std::stop_token token);

  Env env_;
  time::TickSpan ticks_per_step_{};
  bool started_ = false;
  std::jthread thread_;
};

}

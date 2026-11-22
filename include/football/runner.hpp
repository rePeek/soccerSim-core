#pragma once

#include <filesystem>
#include <thread>

#include "football/env.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::runner {

class SingleRunner {
 public:
  SingleRunner(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               time::TickSpan tick_limit = kDefaultRunTicks,
               std::filesystem::path history_path = {});
  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  void Start();

  void Wait();

  const Env& Environment() const;

 private:
  Env env_;
  time::TickSpan tick_limit_{};
  bool started_ = false;
  std::jthread thread_;
};

}

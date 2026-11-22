#pragma once

#include <stop_token>
#include <thread>

#include "football/env.hpp"

namespace football::runner {

class SingleRunner {
 public:
  SingleRunner(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               history::Recording recording = history::Recording::Enabled);
  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  void Wait();

  void Stop();

  Env& Environment();
  const Env& Environment() const;

 private:
  void Loop(std::stop_token token);

  Env env_;
  std::jthread thread_;
};

}

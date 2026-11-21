#include "football/runner.hpp"

#include <stdexcept>

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(pitch::PitchModel pitch_model,
                           ball::BallModel ball_model,
                           history::Recording recording,
                           time::TickSpan ticks_per_step)
    : env_(pitch_model, ball_model, recording),
      ticks_per_step_(ticks_per_step) {}

SingleRunner::~SingleRunner() {
  Stop();
  Info(Channel::env, "runner destroyed");
}

void SingleRunner::Start() {
  if (started_) return;
  started_ = true;
  thread_ = std::jthread([this](std::stop_token token) { Loop(token); });
  Info(Channel::env, "runner worker started");
}

void SingleRunner::Wait() {
  if (thread_.joinable()) thread_.join();
}

void SingleRunner::Stop() {
  if (!thread_.joinable()) return;
  thread_.request_stop();
  thread_.join();
}

const history::History& SingleRunner::history() const { return env_.history(); }

const Env& SingleRunner::EnvironmentAfterJoin() const {
  if (thread_.joinable()) {
    throw std::logic_error("worker must be joined before accessing Env");
  }
  return env_;
}

void SingleRunner::Loop(std::stop_token token) {
  while (!token.stop_requested() && !env_.Finished()) {
    try {
      if (ticks_per_step_.value == 0) {
        env_.Step();
      } else {
        env_.Step(ticks_per_step_);
      }
    } catch (...) {
      Error(Channel::env, "runner caught an exception and stopped");
      return;
    }
  }

  if (!token.stop_requested()) Info(Channel::env, "runner reached full time");
  Info(Channel::env, "runner loop exited");
}

}

#include "football/runner.hpp"

#include <stdexcept>

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(pitch::PitchModel pitch_model,
                           ball::BallModel ball_model,
                           history::Recording recording,
                           time::TickSpan tick_limit)
    : env_(pitch_model, ball_model, recording), tick_limit_(tick_limit) {}

SingleRunner::~SingleRunner() {
  Wait();
  Info(Channel::env, "runner destroyed");
}

void SingleRunner::Start() {
  if (started_) return;
  started_ = true;
  thread_ = std::jthread([this] { env_.LoopMain(tick_limit_); });
  Info(Channel::env, "runner started");
}

void SingleRunner::Wait() {
  if (thread_.joinable()) thread_.join();
}

const history::History& SingleRunner::history() const { return env_.history(); }

const Env& SingleRunner::EnvironmentAfterJoin() const {
  if (thread_.joinable()) {
    throw std::logic_error("worker must be joined before accessing Env");
  }
  return env_;
}

}

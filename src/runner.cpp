#include "football/runner.hpp"

#include <utility>

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(pitch::PitchModel pitch_model,
                           ball::BallModel ball_model,
                           time::TickSpan tick_limit,
                           std::filesystem::path history_path)
    : env_(pitch_model, ball_model, std::move(history_path)),
      tick_limit_(tick_limit) {}

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

const Env& SingleRunner::Environment() const { return env_; }

}

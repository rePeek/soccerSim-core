#include "football/runner.hpp"

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(pitch::PitchModel pitch_model,
                           ball::BallModel ball_model, history::Recording recording)
    : env_(pitch_model, ball_model, recording) {
  thread_ = std::jthread([this](std::stop_token token) { Loop(token); });
  Info(Channel::env, "runner worker started");
}

SingleRunner::~SingleRunner() {
  Stop();
  Info(Channel::env, "runner destroyed");
}

void SingleRunner::Wait() {
  if (thread_.joinable()) thread_.join();
}

void SingleRunner::Stop() {
  if (!thread_.joinable()) return;
  thread_.request_stop();
  thread_.join();
}

Env& SingleRunner::Environment() { return env_; }

const Env& SingleRunner::Environment() const { return env_; }

void SingleRunner::Loop(std::stop_token token) {
  while (!token.stop_requested() && !env_.Finished()) {
    try {
      env_.Step();
    } catch (...) {
      Error(Channel::env, "runner caught an exception and stopped");
      return;
    }
  }

  if (!token.stop_requested()) Info(Channel::env, "runner reached full time");
  Info(Channel::env, "runner loop exited");
}

}

#include "football/runner.hpp"

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(pitch::PitchModel pitch_model,
                           ball::BallModel ball_model, bool record)
    : env_(pitch_model, ball_model, record) {
  // The worker is the only executor, so construction starts it and returns: a
  // host never waits for a match to be played, and no control surface is needed to
  // keep it going.
  thread_ = std::jthread([this](std::stop_token token) { Loop(token); });
  Info(Channel::env, "runner worker started");
}

SingleRunner::~SingleRunner() {
  // Reap the worker before the match and the log line go away; Stop() never
  // waits for full time.
  Stop();
  Info(Channel::env, "runner destroyed");
}

void SingleRunner::Wait() {
  // Joining without a stop request lets the match play out to full time; a loop
  // that already ended leaves nothing to join.
  if (thread_.joinable()) thread_.join();
}

void SingleRunner::Stop() {
  // One-way teardown: ask the loop to end, then reap the thread. Stopping touches
  // only the loop, so the match is left exactly where it stopped and stays
  // readable through Environment().
  if (!thread_.joinable()) return;
  thread_.request_stop();
  thread_.join();
}

Env& SingleRunner::Environment() { return env_; }

const Env& SingleRunner::Environment() const { return env_; }

void SingleRunner::Loop(std::stop_token token) {
  // Bounded by the match: once the environment reports full time the loop ends,
  // so a finished match leaves no worker behind.
  while (!token.stop_requested() && !env_.Finished()) {
    try {
      env_.Step();
    } catch (...) {
      // Exceptions never cross the thread boundary, so a broken tick is reported
      // here and the loop ends instead of taking the process down.
      Error(Channel::env, "runner caught an exception and stopped");
      return;
    }
  }

  if (!token.stop_requested()) Info(Channel::env, "runner reached full time");
  Info(Channel::env, "runner loop exited");
}

}  // namespace football::runner

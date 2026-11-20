#include "football/runner.hpp"

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(PitchModel pitch_model, BallModel ball_model)
    : env_(pitch_model, ball_model) {
  // The worker counts as alive from here, so IsRunning() cannot report a stale
  // "playing" state between construction and the first loop iteration.
  loop_active_.store(true, std::memory_order_release);
  thread_ = std::jthread([this](std::stop_token token) { Loop(token); });
  Info(Channel::env, "runner worker started");
}

SingleRunner::~SingleRunner() {
  // The jthread requests stop and joins; the match is left as it was.
  Info(Channel::env, "runner destroyed");
}

// ---- requests ----

void SingleRunner::Resume() {
  // This only records the intent: whether it has any effect is the loop's call,
  // so resuming a finished match is a harmless no-op instead of a special case.
  const bool was_running =
      run_requested_.exchange(true, std::memory_order_acq_rel);
  if (was_running) return;  // idempotent: nothing to publish, nobody to wake

  Info(Channel::env, "runner resume requested");
  Notify();
}

void SingleRunner::Pause() {
  const bool was_running =
      run_requested_.exchange(false, std::memory_order_acq_rel);
  if (was_running) Info(Channel::env, "runner paused");
  Notify();

  // Wait for the worker to confirm it is parked, so the caller may read the match
  // without sharing it with a running loop. A finished or destroyed loop has no
  // worker left to wait for.
  std::unique_lock lock(mutex_);
  wake_.wait(lock, [this] {
    return parked_.load(std::memory_order_relaxed) ||
           !loop_active_.load(std::memory_order_relaxed);
  });
}

bool SingleRunner::IsRunning() const {
  return run_requested_.load(std::memory_order_acquire) &&
         loop_active_.load(std::memory_order_acquire);
}

Env& SingleRunner::Environment() { return env_; }

const Env& SingleRunner::Environment() const { return env_; }

// ---- the loop ----

void SingleRunner::Loop(std::stop_token token) {
  // Bounded by the match: once the environment reports full time the loop ends,
  // so a finished match leaves no parked worker behind.
  while (!token.stop_requested() && !env_.Finished()) {
    if (!run_requested_.load(std::memory_order_acquire)) {
      Debug(Channel::env, "runner parked: not resumed");
      WaitForResume(token);
      continue;
    }

    try {
      env_.Step();
    } catch (...) {
      // Exceptions never cross the thread boundary, so a broken tick is reported
      // here and the loop ends instead of taking the process down.
      Error(Channel::env, "runner caught an exception and stopped");
      run_requested_.store(false, std::memory_order_release);
      loop_active_.store(false, std::memory_order_release);
      Notify();
      return;
    }
  }

  loop_active_.store(false, std::memory_order_release);
  Notify();  // release a Pause() that is waiting for the worker
  if (!token.stop_requested()) Info(Channel::env, "runner reached full time");
  Info(Channel::env, "runner loop exited");
}

void SingleRunner::WaitForResume(std::stop_token token) {
  std::unique_lock lock(mutex_);
  // Announce the park before sleeping: this is the handshake Pause() waits for.
  parked_.store(true, std::memory_order_release);
  wake_.notify_all();

  // Waiting with the token means a request_stop (i.e. destruction) also wakes us;
  // the predicate only has to describe the runner side.
  wake_.wait(lock, token,
             [this] { return run_requested_.load(std::memory_order_relaxed); });
  parked_.store(false, std::memory_order_release);
}

void SingleRunner::Notify() {
  // The lock makes the wakeup ordering obvious against the predicates above; no
  // environment state is read or written under it.
  { std::lock_guard lock(mutex_); }
  wake_.notify_all();
}

}  // namespace football::runner

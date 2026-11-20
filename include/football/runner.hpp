#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

#include "football/env.hpp"

namespace football::runner {

// One match and one worker: the runner owns both and is the whole execution
// context of a single match, which is what the C ABI handle binds to.
//
// It owns the only execution state there is — whether the loop should keep calling
// Env::Step() — and knows nothing about time or the state of the match: playing,
// pausing and stopping are decisions of this object, never of the environment.
//
// The loop thread is the only code that touches the environment while it runs. A
// host reads it through Environment(), which is safe because Pause() returns only
// once the worker is parked, and the loop no longer touches the match after it has
// ended.
class SingleRunner {
 public:
  SingleRunner(PitchModel pitch_model, BallModel ball_model);
  // Stops the loop and joins it.
  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  // Requests, idempotent and safe from any thread. Resuming a finished match does
  // nothing, and a paused runner simply stops calling Step().
  void Resume();
  // Stops advancing and returns only once the worker is parked, so the match can be
  // read afterwards without sharing it with a running loop.
  void Pause();
  // Asked to run while its worker is alive: it goes false both when the host
  // pauses and when the loop ends, and says nothing about the tick.
  bool IsRunning() const;

  // The match this runner owns. Read it while paused, or once the loop has ended.
  Env& Environment();
  const Env& Environment() const;

 private:
  void Loop(std::stop_token token);
  void WaitForResume(std::stop_token token);
  void Notify();

  Env env_;
  std::atomic<bool> run_requested_{false};
  // False once the loop has ended, whatever the reason. Thread state, not match
  // state: it only tells a host whether the worker is still alive.
  std::atomic<bool> loop_active_{false};
  // True while the worker is parked, which is what Pause() waits for.
  std::atomic<bool> parked_{false};
  // Guards nothing but the parking handshake: the environment is never touched
  // under it.
  std::mutex mutex_;
  std::condition_variable_any wake_;
  // A jthread owns its own stop source, so teardown is thread lifetime rather
  // than a runner state: destroying the runner stops and joins the loop.
  std::jthread thread_;
};

}  // namespace football::runner

#pragma once

#include <stop_token>
#include <thread>

#include "football/env.hpp"

namespace football::runner {

// One match and one worker: the runner owns both and is the whole execution
// context of a single match, which is what the C ABI handle binds to.
//
// It holds no control state - nothing resumes, pauses or reports progress - so the
// worker plays the match as soon as the runner exists. The loop thread is the only
// code that touches the environment while it runs, and Wait() returns only once
// that loop has ended, which is what makes reading the match safe.
class SingleRunner {
 public:
  SingleRunner(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               bool record = true);
  // Stops the loop and joins it; a match short of full time is left as it was.
  ~SingleRunner();

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  // Blocks until the loop has ended - at full time, after a broken tick or after a
  // stop request. Idempotent, and safe once the loop has already ended.
  void Wait();

  // Ends the loop now and reaps the worker; the match stays where it stopped and
  // remains readable. One-way: the run cannot be restarted, only rebuilt.
  void Stop();

  // The match this runner owns, safe to read once Wait() has returned.
  Env& Environment();
  const Env& Environment() const;

 private:
  void Loop(std::stop_token token);

  Env env_;
  // A jthread owns its own stop source, so teardown is thread lifetime rather
  // than runner state: destroying the runner stops and joins the loop.
  std::jthread thread_;
};

}  // namespace football::runner

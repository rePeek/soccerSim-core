#pragma once

#include "football/env.hpp"

namespace football::runner {

// One match: the runner owns it and is the whole execution context of a single
// match, which is what the C ABI handle binds to.
//
// It holds no control state - nothing resumes, pauses or reports it - so the run
// is its construction: the match plays to full time before the object exists, and
// Environment() exposes a complete match nobody else touches.
class SingleRunner {
 public:
  SingleRunner(PitchModel pitch_model, BallModel ball_model);

  SingleRunner(const SingleRunner&) = delete;
  SingleRunner& operator=(const SingleRunner&) = delete;
  SingleRunner(SingleRunner&&) = delete;
  SingleRunner& operator=(SingleRunner&&) = delete;

  // The match this runner owns, complete because the run ended in construction.
  Env& Environment();
  const Env& Environment() const;

 private:
  Env env_;
};

}  // namespace football::runner

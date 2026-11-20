#include "football/runner.hpp"

#include "football/log/log.hpp"

using namespace football::log;

namespace football::runner {

SingleRunner::SingleRunner(PitchModel pitch_model, BallModel ball_model)
    : env_(pitch_model, ball_model) {
  // The run is the constructor: with no control surface there is nothing to
  // resume, so the match simply plays until it reports full time.
  while (!env_.Finished()) {
    try {
      env_.Step();
    } catch (...) {
      // Exceptions never cross a host call: a broken tick is reported and the
      // match stays where it stopped.
      Error(Channel::env, "runner caught an exception and stopped");
      return;
    }
  }
  Info(Channel::env, "runner reached full time");
}

Env& SingleRunner::Environment() { return env_; }

const Env& SingleRunner::Environment() const { return env_; }

}  // namespace football::runner

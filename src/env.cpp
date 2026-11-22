#include "football/env.hpp"

#include <cstdint>
#include <utility>

#include "football/log/log.hpp"

using namespace football::log;

namespace football {

Env::Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
         history::History history)
    : pitch_(pitch_model), ball_(ball_model), history_(std::move(history)) {
  if (history_.mode() == history::HistoryMode::Recording) {
    ball_.PlaceOnGround(0.0f, 0.0f);
    history_.RecordSnapshot(tick_, ball_.get_state());
  }
  Info(Channel::env, "environment constructed");
}

Env::~Env() { Info(Channel::env, "environment destroyed"); }

void Env::Step() {
  ball_.Step(pitch_);
  tick_ += time::TickSpan{1};
  history_.RecordSnapshot(tick_, ball_.get_state());
}

void Env::LoopMain(time::TickSpan tick_limit) {
  if (history_.mode() == history::HistoryMode::Replay) {
    Info(Channel::env, "replay environment does not simulate");
    return;
  }
  for (std::uint64_t advanced = 0; advanced < tick_limit.value; ++advanced) {
    try {
      Step();
    } catch (...) {
      Error(Channel::env, "simulation caught an exception and stopped");
      return;
    }
  }
  Info(Channel::env, "simulation reached its tick limit");
}

time::Tick Env::Ticks() const { return tick_; }

}

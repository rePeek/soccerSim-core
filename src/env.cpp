#include "football/env.hpp"

#include <cstddef>

#include "football/log/log.hpp"

using namespace football::log;

namespace football {

Env::Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
         history::Recording recording)
    : pitch_(pitch_model),
      ball_(ball_model),
      history_(static_cast<std::size_t>(kFullTime.value) + 1, recording) {
  ball_.PlaceOnGround(0.0f, 0.0f);
  history_.RecordSnapshot(tick_, ball_.get_state());
  Info(Channel::env, "environment constructed");
}

Env::~Env() { Info(Channel::env, "environment destroyed"); }

void Env::Step() {
  ball_.Step(pitch_);
  tick_ += time::TickSpan{1};
  phase_ = PhaseOfTick(tick_.value);
  history_.RecordSnapshot(tick_, ball_.get_state());
}

void Env::Step(time::TickSpan duration) {
  for (std::uint64_t i = 0; i < duration.value && !Finished(); ++i) {
    Step();
  }
}

time::Tick Env::Ticks() const { return tick_; }

MatchPhase Env::Phase() const { return phase_; }

bool Env::Finished() const { return phase_ == MatchPhase::FullTime; }

}

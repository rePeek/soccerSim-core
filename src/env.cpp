#include "football/env.hpp"

#include "football/log/log.hpp"

using namespace football::log;

namespace football {

Env::Env(pitch::PitchModel pitch_model, ball::BallModel ball_model)
    : pitch_(pitch_model), ball_(ball_model) {
  // The centre spot is the only sensible start until a caller moves the ball.
  ball_.PlaceOnGround(0.0f, 0.0f);
  Info(Channel::env, "environment constructed");
}

Env::~Env() { Info(Channel::env, "environment destroyed"); }

void Env::Step() {
  ball_.Step(pitch_);
  tick_ += time::TickSpan{1};
  phase_ = PhaseOfTick(tick_.value);
}

void Env::Step(time::TickSpan duration) {
  for (std::uint64_t i = 0; i < duration.value && !Finished(); ++i) {
    Step();
  }
}

time::Tick Env::Ticks() const { return tick_; }

MatchPhase Env::Phase() const { return phase_; }

bool Env::Finished() const { return phase_ == MatchPhase::FullTime; }

}  // namespace football

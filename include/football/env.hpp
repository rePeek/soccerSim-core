#pragma once

#include <cstdint>

#include "football/ball/ball.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/history.hpp"
#include "football/pitch/pitch.hpp"

namespace football {

inline constexpr time::Tick kHalfTime{time::Minutes(45).value};
inline constexpr time::Tick kFullTime{time::Minutes(90).value};
static_assert(kHalfTime.value == 270000, "45 minutes at 100 Hz");
static_assert(kFullTime.value == 540000, "90 minutes at 100 Hz");

enum class MatchPhase { FirstHalf, HalfTime, SecondHalf, FullTime };

constexpr MatchPhase PhaseOfTick(std::uint64_t tick) {
  if (tick >= kFullTime.value) return MatchPhase::FullTime;
  if (tick == kHalfTime.value) return MatchPhase::HalfTime;
  if (tick < kHalfTime.value) return MatchPhase::FirstHalf;
  return MatchPhase::SecondHalf;
}

class Env {
 public:
  explicit Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               history::Recording recording = history::Recording::Enabled);
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

  void Step();
  void Step(time::TickSpan duration);

  const history::History& history() const { return history_; }

  time::Tick Ticks() const;
  MatchPhase Phase() const;
  bool Finished() const;

 private:
  pitch::Pitch pitch_;
  ball::Ball ball_;
  history::History history_;
  time::Tick tick_{0};
  MatchPhase phase_{MatchPhase::FirstHalf};
};

}

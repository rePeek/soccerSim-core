#pragma once

#include <cstdint>

#include "football/ball/ball.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"

namespace football {

// Default half and full time: 270000 and 540000 ticks = 45 and 90 minutes at
// the native 100 Hz rate. There is no clock or added time: both boundaries are
// fixed tick counts, and the last simulated tick is kFullTime - 1.
inline constexpr time::Tick kHalfTime{time::Minutes(45).value};
inline constexpr time::Tick kFullTime{time::Minutes(90).value};
static_assert(kHalfTime.value == 270000, "45 minutes at 100 Hz");
static_assert(kFullTime.value == 540000, "90 minutes at 100 Hz");

// The phase a match is in. Half time is the instant `kHalfTime` is reached: one
// tick wide, with no break, ceremony or clock stop of its own.
enum class MatchPhase { FirstHalf, HalfTime, SecondHalf, FullTime };

// The one place the boundaries are turned into a phase. `Env::Step()` calls it to
// update the phase it stores, so anything that reports progress derives the
// phase from the same rule. Half time is the single tick at kHalfTime, so a
// caller that wants a break to last holds the match there; full time is
// reached at kFullTime and stays.
constexpr MatchPhase PhaseOfTick(std::uint64_t tick) {
  if (tick >= kFullTime.value) return MatchPhase::FullTime;
  if (tick == kHalfTime.value) return MatchPhase::HalfTime;
  if (tick < kHalfTime.value) return MatchPhase::FirstHalf;
  return MatchPhase::SecondHalf;
}

// One match's state and simulation rules: the ball, the pitch and the tick
// cursor. It advances only when someone calls Step(), so it holds no thread, no
// lock and no notion of playing or pausing.
//
// Nothing here is synchronised: one executor owns the environment and is the only
// code that advances or reads it. A host reads this object only once that
// executor has stopped, never while its loop runs.
class Env {
 public:
  explicit Env(pitch::PitchModel pitch_model, ball::BallModel ball_model);
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

  // Physics of exactly one tick (10 ms of simulated time). It does not police the
  // horizon: the executor stops calling it once Finished() reports full time.
  void Step();
  // The same step, repeated: advances up to `duration` ticks, stopping at full
  // time.
  void Step(time::TickSpan duration);

  time::Tick Ticks() const;
  MatchPhase Phase() const;
  bool Finished() const;

 private:
  pitch::Pitch pitch_;
  ball::Ball ball_;
  time::Tick tick_{0};
  // Mirrors the cursor so Phase() is a plain read; Step() is the only writer.
  MatchPhase phase_{MatchPhase::FirstHalf};
};

}  // namespace football

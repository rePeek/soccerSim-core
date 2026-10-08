#ifndef FOOTBALL_SIM_MATCH_CLOCK_HPP
#define FOOTBALL_SIM_MATCH_CLOCK_HPP

#include <cstdint>

#include "sim/match/match_phase.hpp"
#include "sim/time/tick.hpp"

namespace football::sim {

// Clock state only: no actors, possession, rule decisions, score, or RNG.
// Phase is supplied by the competition owner, never duplicated inside the clock.
class MatchClock {
 public:
  explicit MatchClock(TickSpan half_duration);

  Tick now() const { return now_; }
  TickSpan RegulationTime() const { return regulation_elapsed_; }
  TickSpan BallInPlayTime() const { return ball_in_play_elapsed_; }
  std::uint64_t ExecutedTicks() const { return duration_ticks_; }
  // Call-local consumers may borrow this gate across accepted-contact commands.
  const bool& IsHalfUnderway() const { return regulation_running_; }
  const bool& IsBallInPlay() const { return ball_in_play_; }

  // BeginHalf means the accepted opening contact, not publication of First/SecondHalf.
  // It is idempotent across later accepted restarts; accumulated clocks never reset.
  void BeginHalf() { regulation_running_ = true; }
  void EndHalf() { StopBallInPlay(); regulation_running_ = false; }
  void StartBallInPlay();
  void StopBallInPlay() { ball_in_play_ = false; }

  // Called at tick entry, before a possible terminal whistle. Manual advances
  // do not count executions; Finished calls do not count or advance anything.
  void CountExecutedStep(MatchPhase phase);
  // Returns admitted regulation time for callers' synchronous derived updates.
  // Checks all arithmetic before publishing any clock; clips football clocks
  // at this period even when a manual timeline jump extends beyond it.
  TickSpan Advance(TickSpan delta, MatchPhase phase);

 private:
  const TickSpan half_duration_;
  Tick now_{};
  std::uint64_t duration_ticks_ = 0;
  TickSpan regulation_elapsed_{};
  TickSpan ball_in_play_elapsed_{};
  bool regulation_running_ = false;
  bool ball_in_play_ = false;
};

}  // namespace football::sim

#endif

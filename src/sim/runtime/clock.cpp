#include "sim/runtime/clock.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace football::sim {

MatchClock::MatchClock(TickSpan half_duration) : half_duration_(half_duration) {
  if (half_duration == TickSpan{} ||
      half_duration.value > std::numeric_limits<std::uint64_t>::max() / 2) {
    throw std::invalid_argument("invalid regulation duration");
  }
}

void MatchClock::StartBallInPlay() {
  if (!regulation_running_)
    throw std::logic_error("ball-in-play clock requires an underway half");
  ball_in_play_ = true;
}

void MatchClock::CountExecutedStep(MatchPhase phase) {
  if (phase != MatchPhase::Finished) ++duration_ticks_;
}

TickSpan MatchClock::Advance(TickSpan delta, MatchPhase phase) {
  if (phase == MatchPhase::Finished) return {};
  const auto next_tick = now_ + delta;
  TickSpan admitted{};
  if (regulation_running_) {
    const auto half = half_duration_;
    const auto limit = phase == MatchPhase::SecondHalf ? half + half : half;
    admitted = TickSpan{std::min(delta.value,
        (limit - std::min(regulation_elapsed_, limit)).value)};
  }
  const auto next_regulation = regulation_elapsed_ + admitted;
  const auto next_in_play = ball_in_play_elapsed_ + (ball_in_play_ ? admitted : TickSpan{});
  now_ = next_tick;
  regulation_elapsed_ = next_regulation;
  ball_in_play_elapsed_ = next_in_play;
  return admitted;
}

}  // namespace football::sim

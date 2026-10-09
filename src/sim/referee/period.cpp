#include "sim/referee/period.hpp"

namespace football::sim::rules {

bool PeriodElapsed(bool half_underway, MatchPhase phase,
                   TickSpan regulation_elapsed, TickSpan half_duration) {
  if (!half_underway) return false;
  const auto half = half_duration;
  const auto limit = phase == MatchPhase::SecondHalf ? half + half : half;
  return regulation_elapsed >= limit;
}

}  // namespace football::sim::rules

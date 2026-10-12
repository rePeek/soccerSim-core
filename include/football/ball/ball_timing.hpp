#ifndef FOOTBALL_BALL_BALL_TIMING_HPP
#define FOOTBALL_BALL_BALL_TIMING_HPP

// Ball prediction policy (a fixed horizon shared by MentalImage reachability
// sampling). Not simulation quantum or a runtime-configurable dt.
#include "football/foundation/time/tick_boundary.hpp"

namespace football::sim::ball_timing {
inline constexpr auto kPredictionHorizon = Seconds(3);
inline constexpr TickSpan kPredictionCache{100};
}  // namespace football::sim::ball_timing

#endif  // FOOTBALL_BALL_BALL_TIMING_HPP
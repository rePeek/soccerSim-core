#ifndef FOOTBALL_SIM_BALL_TIMING_HPP
#define FOOTBALL_SIM_BALL_TIMING_HPP

#include "sim/tick.hpp"

namespace football::sim::ball_timing {
// Ball prediction policy, not simulation quantum or a runtime-configurable dt.
inline constexpr auto kPredictionHorizon = Seconds(3);
inline constexpr TickSpan kPredictionCache{100};
}  // namespace football::sim::ball_timing

#endif

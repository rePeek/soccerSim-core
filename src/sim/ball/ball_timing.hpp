#ifndef FOOTBALL_SIM_BALL_TIMING_HPP
#define FOOTBALL_SIM_BALL_TIMING_HPP

#include "sim/time/tick_boundary.hpp"

namespace football::sim::ball_timing {
// Ball prediction policy, not simulation quantum or a runtime-configurable dt.
inline constexpr auto kPredictionHorizon = Seconds(3);
inline constexpr TickSpan kPredictionCache{100};
}  // namespace football::sim::ball_timing

// Temporary millisecond projections for calculation callers migrating next.
const unsigned int ballPredictionSize_ms = football::sim::ToMilliseconds(
    football::sim::ball_timing::kPredictionHorizon);
const unsigned int cachedPredictions = football::sim::ball_timing::kPredictionCache.value;
const float ballDistanceOptimizeThreshold = 10.0f;

#endif

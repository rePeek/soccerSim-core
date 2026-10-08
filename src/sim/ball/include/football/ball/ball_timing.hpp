#ifndef FOOTBALL_BALL_BALL_TIMING_HPP
#define FOOTBALL_BALL_BALL_TIMING_HPP

// Transitional prediction-timing policy. It remains public while
// MentalImage/Player still size their prediction vectors against the legacy
// horizon; after those consumers converge on Ball's stable Predict() API this
// header should become a private ball implementation detail.
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

#endif  // FOOTBALL_BALL_BALL_TIMING_HPP

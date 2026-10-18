#pragma once

#include "football/foundation/time/tick_boundary.hpp"

namespace football::sim::ball_timing {
inline constexpr auto kPredictionHorizon = Seconds(3);
inline constexpr TickSpan kPredictionCache{100};
}
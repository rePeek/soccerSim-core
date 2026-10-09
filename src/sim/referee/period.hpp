#ifndef FOOTBALL_SIM_RULES_PERIOD_HPP
#define FOOTBALL_SIM_RULES_PERIOD_HPP

#include "sim/runtime/phase.hpp"
#include "foundation/time/tick.hpp"

namespace football::sim::rules {

// Read-only regulation boundary. Ceremonies are not underway halves; ordinary
// dead balls are. Duration is the validated MatchOptions/MatchClock half duration.
// The caller owns whistles, phase publication and all restart/end-half effects.
bool PeriodElapsed(bool half_underway, MatchPhase phase,
                   TickSpan regulation_elapsed, TickSpan half_duration);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_PERIOD_HPP

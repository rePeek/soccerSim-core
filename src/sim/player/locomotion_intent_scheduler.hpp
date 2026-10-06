// Copyright 2026
#ifndef FOOTBALL_SIM_LOCOMOTION_INTENT_SCHEDULER_HPP
#define FOOTBALL_SIM_LOCOMOTION_INTENT_SCHEDULER_HPP

#include "sim/player/player_decision_scheduler.hpp"

// Separate execution/publication cadence. Reuse the existing policy intervals
// without tying refreshes to animation opportunities or a configurable dt.
struct LocomotionIntentScheduler {
  football::sim::Tick next_refresh_tick{};
  int refreshes = 0; // Telemetry count, not a duration or serialized clock.

  static football::sim::TickSpan CadenceForDistance(float distance_to_ball,
                                                   bool has_possession) {
    using namespace football::sim::player_timing;
    if (has_possession) return kOwnerNear;
    if (distance_to_ball < 5.0f) return kNearBall;
    if (distance_to_ball < 10.0f) return kApproachingBall;
    return kIdle;
  }
  bool Due(football::sim::Tick now) const { return now >= next_refresh_tick; }
  void Schedule(football::sim::Tick now, football::sim::TickSpan cadence) {
    next_refresh_tick = now + cadence;
    ++refreshes;
  }
};

#endif

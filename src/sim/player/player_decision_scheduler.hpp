// Copyright 2026
#ifndef FOOTBALL_SIM_PLAYER_DECISION_SCHEDULER_HPP
#define FOOTBALL_SIM_PLAYER_DECISION_SCHEDULER_HPP

#include "sim/tick.hpp"

namespace football::sim::player_timing {
inline constexpr TickSpan kOwnerNear{2};
inline constexpr TickSpan kOwner{3};
inline constexpr TickSpan kTeamOwner{4};
inline constexpr TickSpan kNearBall{5};
inline constexpr TickSpan kApproachingBall{8};
inline constexpr TickSpan kIdle{24};
inline constexpr TickSpan kTacticalRefresh{10};
}  // namespace football::sim::player_timing

// Context-dependent policy cadence, not the definition of simulation time.
struct PlayerDecisionScheduler {
  football::sim::Tick last_refresh_tick{};
  bool initialized = false;

  bool Due(football::sim::Tick now, football::sim::TickSpan cadence) const {
    return !initialized || (now >= last_refresh_tick && now - last_refresh_tick >= cadence);
  }
  void Commit(football::sim::Tick now) {
    last_refresh_tick = now;
    initialized = true;
  }
};

inline football::sim::TickSpan PlayerDecisionCadenceForContext(
    bool designated_possession_player, bool designated_team_possession_player,
    float distance_to_ball) {
  using namespace football::sim::player_timing;
  if (designated_possession_player && distance_to_ball < 3.0f) return kOwnerNear;
  if (designated_possession_player) return kOwner;
  if (designated_team_possession_player) return kTeamOwner;
  if (distance_to_ball < 5.0f) return kNearBall;
  if (distance_to_ball < 10.0f) return kApproachingBall;
  return kIdle;
}

#endif

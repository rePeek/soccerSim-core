#ifndef FOOTBALL_SIM_PLAYER_MATCH_STATE_HPP
#define FOOTBALL_SIM_PLAYER_MATCH_STATE_HPP

#include "football/contact/player_touch_type.hpp"
#include "football/foundation/time/tick.hpp"

// The Player's own match state: personal facts that change during a match and
// belong to the actor alone. Team facts (possession weight, direction, the
// designated player) are Runtime-owned and arrive per tick through the borrowed
// PlayerTeamView; they are deliberately not part of this struct.
struct PlayerMatchState {
  football::sim::Tick last_touch_tick{};
  e_TouchType last_touch_type = e_TouchType_None;
  // 1.0 = rested, 0.01 = exhausted. Decays with distance run.
  float fatigue_factor_inv = 0.0f;
  int cards = 0;  // 1 yellow; 2 second yellow; >= 3 red
  football::sim::Tick card_effective_tick{};
  // The actor's own desired interception time, decayed each tick and published
  // by the reachability refresh. Intent, not a world relation.
  int desired_time_to_ball_ms = 0;
  // A controlled-ball collision already fired for this situation. Cleared by a
  // situation reset; it gates a repeat trigger within the same situation.
  bool controlled_ball_collision_triggered = false;
};

#endif  // FOOTBALL_SIM_PLAYER_MATCH_STATE_HPP

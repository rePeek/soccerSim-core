#ifndef FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#include "football/player/player_team_match_facts.hpp"

namespace football::sim {

// Team-level match facts that cannot be derived from any single player. Runtime
// owns the storage and hands the actor-readable base subobject to a borrowed
// TeamView, so a same-tick designation is visible without a copy.
struct TeamMatchState : football::player::PlayerTeamMatchFacts {
  float possession_amount = 0.0f;
  float fading_possession_amount = 0.0f;
};

// Runtime owns the only live team orientation. The two directions are distinct
// and are deliberately not merged into one field:
//
//   field                writer                   when it may change
//   defending_direction  SwitchTeamEnds           the permanent change of ends
//   execution_direction  MirrorTeam / SwitchEnds  every processing-frame toggle
//
// The historical `TeamFrame` stored `dynamic_side`/`static_side`/`mirrored`; the
// first two were the same two values under names that hid their meaning, and
// `mirrored` was write-only. A field can no longer express both the storage
// frame and the attack direction at once.
//
// Invariants:
// - Simulation owns the only TeamOrientation. Team, Player, Pitch, Referee and
//   AI never own or mutate one; a Player only reads the published facts.
// - SwitchTeamEnds is the permanent change of ends: it flips both directions
//   together.
// - MirrorTeam is the transient execution-frame toggle: it flips only
//   execution_direction and never touches defending_direction.
// - defending_direction is the end the team defends in the pitch; it is the
//   value that survives a processing order and belongs in the Stadium Frame.
// - TeamSide::Home/Away is identity only: it never means left/right and never
//   changes when ends switch or frames mirror.
struct TeamOrientation {
  // The end this team defends in the Stadium Frame: -1 (-X) or +1 (+X).
  int defending_direction = -1;
  // The direction of the team's current execution frame: -1 or +1.
  int execution_direction = -1;

  // The direction this team attacks while it executes in its execution frame.
  int attack_direction() const { return -execution_direction; }
  // True while the execution frame matches the team's persistent end.
  bool on_original_side() const { return execution_direction == defending_direction; }
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

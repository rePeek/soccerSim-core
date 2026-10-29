#ifndef FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP
#include "football/player/execution/player_team_match_facts.hpp"

class Player;

namespace football::sim {

// Team-level match facts that cannot be derived from any single player. Runtime
// owns the storage and hands the actor-readable base subobject to a borrowed
// TeamView, so a same-tick designation is visible without a copy.
struct TeamMatchState : football::player::PlayerTeamMatchFacts {
  // Live actor for the stable `designated_player` identity above. Runtime-only
  // execution cache; Runtime sets it through SetDesignatedPlayer.
  Player* designated_actor = nullptr;
  // The team's active goalkeeper. Runtime-owned match state: a sent-off
  // goalkeeper hands the role over without any Player caching it.
  Player* goalie = nullptr;

  void SetDesignatedPlayer(Player* player);
};

// Runtime owns the only live team orientation. There is exactly one physical
// coordinate system, the Stadium Frame, and a team has exactly one direction in
// it: the end it defends. The attacking direction is always its negation, so a
// single integer is the whole live orientation.
//
// Invariants:
// - Simulation owns the only TeamOrientation. Team, Player, Pitch, Referee and
//   AI never own or mutate one; a Player only reads the published facts.
// - SwitchTeamEnds is the only writer: the permanent half-time change of ends
//   flips defending_direction and rewrites nothing else.
// - TeamSide::Home/Away is identity only: it never means left/right and never
//   changes when ends switch.
struct TeamOrientation {
  // The end this team defends in the Stadium Frame: -1 (-X) or +1 (+X).
  int defending_direction = -1;

  // The direction this team attacks in the Stadium Frame.
  int attack_direction() const { return -defending_direction; }
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

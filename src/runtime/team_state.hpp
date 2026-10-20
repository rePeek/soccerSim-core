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

// Runtime owns the only live orientation values; Team owns static templates and
// an actor owns none of them. The three fields are distinct and must not be
// merged. Their writers and allowed transitions are a closed contract:
//
//   field          writer                        when it may change
//   static_side    Runtime::SwitchTeamEnds       the half-time change of ends only
//   dynamic_side   Runtime::MirrorTeam and       every processing-frame toggle and
//                  Runtime::SwitchTeamEnds      the half-time change of ends
//   mirrored       Runtime::MirrorTeam            transient mirror only
//
// Invariants:
// - Simulation owns the only TeamFrame. Team, Player, Pitch, Referee and AI
//   never own or mutate one; a Player only reads the published facts.
// - SwitchTeamEnds is the permanent change of ends: it flips static_side and
//   dynamic_side together and leaves mirrored untouched.
// - MirrorTeam is the transient execution-frame toggle: it flips dynamic_side
//   and mirrored together and never touches static_side.
// - dynamic_side is the direction of the current execution frame (the mirror
//   counter) and is what actors read.
// - static_side is the direction the team defends in the pitch.
// - mirrored is the runtime coordinate-transform state between algorithm
//   phases, not a team identity or a pitch direction.
// - TeamSide::Home/Away is identity only: it never means left/right and never
//   changes when ends switch or frames mirror.
struct TeamFrame {
  int dynamic_side = -1;
  int static_side = -1;
  bool mirrored = false;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_TEAM_STATE_HPP

#pragma once

#include <map>
#include <string>
#include <vector>

#include "football/player/player_definition.hpp"
#include "football/team/formation.hpp"
#include "football/team/formation_preset.hpp"
#include "football/team/leadership.hpp"
#include "football/team/lineup.hpp"
#include "football/team/set_piece_orders.hpp"

namespace football::team {

// Static team-domain entity: identity, declared personnel, appointments,
// reusable restart orders and formation templates.
//
// It deliberately owns no Player, Roster, physics, match state, Referee or AI
// controller. Runtime composes two of these with its own Roster, TeamFrame,
// TeamMatchState and ActiveLineupState. `initial_tactics` is bootstrap input for
// the AI board, never a second writable TacticalBoard.
struct Team {
  std::string name;
  std::vector<football::player::PlayerDefinition> players;

  Lineup lineup;
  Leadership leadership;
  SetPieceOrders set_piece_orders;
  FormationPresets formations;

  InitialFormation initial_formation;
  TacticalFormation tactical_formation;
  std::map<std::string, float> initial_tactics;
};

// Pure validation; throws std::invalid_argument. Player objects/state are not
// involved, and no RNG is consumed.
void ValidateTeam(const Team& team);
void ValidateTeams(const Team& home, const Team& away);

}  // namespace football::team

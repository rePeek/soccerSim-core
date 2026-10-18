#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>
#include "football/runtime/formation.hpp"
#include "football/runtime/formation_preset.hpp"
#include "football/runtime/team_roles.hpp"

namespace football::sim {
struct TeamSetup;
// Match-level configuration only. Once actors borrow players, Runtime must not
// move/assign this Team or resize players until those actors have been destroyed.
struct Team {
  std::string name;
  std::vector<football::player::PlayerDefinition> players;
  TeamLineup initial_lineup;
  TeamLeadership leadership;
  SetPieceOrders set_piece_orders;
  InitialFormation initial_formation;
  TacticalFormation tactical_formation;
  FormationPresetLibrary formation_presets;
  std::map<std::string, float> legacy_tactics;  // bootstrap input, never live board
  bool explicit_lineup = false;  // compatibility construction mode
};
// Pure conversion; no identities generated, no RNG, no actor references.
Team MakeTeam(const TeamSetup& setup);
void ValidateTeam(const Team& team);  // throws invalid_argument
void ValidateMatchTeams(const std::array<Team, 2>& teams);
}  // namespace football::sim

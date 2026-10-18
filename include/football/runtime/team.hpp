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
// Transitional preparation values; moved to Runtime-private headers in TEAM-5.5.
// The live Team owns config only; definitions are consumed by actor construction.
struct TeamConfig {
  std::string name;
  TeamLineup initial_lineup;
  TeamLeadership leadership;
  SetPieceOrders set_piece_orders;
  InitialFormation initial_formation;
  TacticalFormation tactical_formation;
  FormationPresetLibrary formation_presets;
  std::map<std::string, float> legacy_tactics;
  bool explicit_lineup = false;
};
struct PreparedTeam : TeamConfig {
  std::vector<football::player::PlayerDefinition> players;
};
PreparedTeam PrepareTeam(const TeamSetup& setup);
void ValidateTeam(const PreparedTeam& team);
void ValidateMatchTeams(const std::array<PreparedTeam, 2>& teams);
}  // namespace football::sim

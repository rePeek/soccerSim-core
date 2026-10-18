#pragma once

#include <map>
#include <string>
#include "football/runtime/formation.hpp"
#include "football/runtime/formation_preset.hpp"
#include "football/runtime/team_roles.hpp"

namespace football::sim {
// Runtime-private initial relationship/configuration data. No independent
// PlayerDefinition array, live TacticalBoard, rule controller or actor service.
struct TeamConfig {
  std::string name;
  TeamLineup initial_lineup;
  TeamLeadership leadership;
  SetPieceOrders set_piece_orders;
  InitialFormation initial_formation;
  TacticalFormation tactical_formation;
  FormationPresetLibrary formation_presets;
  std::map<std::string, float> legacy_tactics;
};
}  // namespace football::sim

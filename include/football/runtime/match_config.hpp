#ifndef FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP
#define FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "football/runtime/formation.hpp"
#include "football/player/player_definition.hpp"
#include "football/runtime/team_lineup.hpp"
#include "football/runtime/team_roles.hpp"
#include "football/runtime/formation_preset.hpp"

namespace football::sim {

// A team's role in a match, not a persistent club identity or pitch direction.
// Home/away do not change when play is mirrored or the teams switch ends.
enum class TeamSide : std::uint8_t { Home = 0, Away = 1 };

// Static team composition, independent of an opponent, controllers and match
// state. Callers supply players explicitly; there is no implicit database roster.
struct TeamSetup {
  std::string name;
  std::vector<football::player::PlayerDefinition> players;
  InitialFormation formation;
  TacticalFormation tactical_formation;
  // Static tactical preferences, not files, serialized Properties or live AI
  // state. Unspecified preferences leave the simulation's algorithm defaults.
  std::map<std::string, float> tactics;
  // External declarations converted once into the match-owned Team.
  std::optional<TeamLineup> lineup;
  TeamLeadership leadership;
  SetPieceOrders set_piece_orders;
  FormationPresetLibrary formation_presets;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP

#ifndef FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP
#define FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "football/player/player_definition.hpp"
#include "football/team/formation.hpp"
#include "football/team/formation_preset.hpp"
#include "football/team/leadership.hpp"
#include "football/team/lineup.hpp"
#include "football/team/set_piece_orders.hpp"

namespace football::sim {

// A team's role in a match, not a persistent club identity or pitch direction.
// Home/away do not change when play is mirrored or the teams switch ends.
enum class TeamSide : std::uint8_t { Home = 0, Away = 1 };

// Transitional external declaration. Runtime converts it once into a
// football::team::TeamDefinition; this type must not become a second live team
// Callers supply players explicitly; there is no implicit database roster.
struct TeamSetup {
  std::string name;
  std::vector<football::player::PlayerDefinition> players;
  football::team::InitialFormation formation;
  football::team::TacticalFormation tactical_formation;
  // Static tactical bootstrap preferences, never a writable TacticalBoard.
  std::map<std::string, float> tactics;
  std::optional<football::team::Lineup> lineup;
  football::team::Leadership leadership;
  football::team::SetPieceOrders set_piece_orders;
  football::team::FormationPresets formation_presets;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_MATCH_CONFIG_HPP

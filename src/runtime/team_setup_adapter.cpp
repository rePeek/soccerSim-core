#include "runtime/team_setup_adapter.hpp"

#include <algorithm>
#include <stdexcept>

namespace football::sim {
namespace {
std::size_t FormationCount(const TeamSetup& setup) {
  return !setup.formation.empty() ? setup.formation.size() :
      !setup.tactical_formation.empty() ? setup.tactical_formation.size() :
      setup.players.size();
}
}  // namespace

PreparedTeam PrepareTeam(const TeamSetup& setup) {
  PreparedTeam prepared;
  auto& team = prepared.team;
  team.name = setup.name;
  team.players = setup.players;
  team.initial_formation = setup.formation;
  team.tactical_formation = setup.tactical_formation;
  team.initial_tactics = setup.tactics;
  team.leadership = setup.leadership;
  team.set_piece_orders = setup.set_piece_orders;
  team.formations = setup.formation_presets;
  prepared.explicit_lineup = setup.lineup.has_value();
  if (setup.lineup) {
    team.lineup = *setup.lineup;
  } else {
    const auto count = std::min(football::team::kFormationSlots,
                                FormationCount(setup));
    if (count > setup.players.size()) {
      throw std::invalid_argument("formation has no player definition");
    }
    for (std::size_t i = 0; i < setup.players.size(); ++i) {
      if (i < count) team.lineup.starters[i] = setup.players[i].id;
      else team.lineup.substitutes.push_back(setup.players[i].id);
    }
  }
  football::team::ValidateTeam(team);
  return prepared;
}

}  // namespace football::sim

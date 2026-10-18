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
  auto& definition = prepared.definition;
  definition.name = setup.name;
  definition.players = setup.players;
  definition.initial_formation = setup.formation;
  definition.tactical_formation = setup.tactical_formation;
  definition.initial_tactics = setup.tactics;
  definition.leadership = setup.leadership;
  definition.set_piece_orders = setup.set_piece_orders;
  definition.formations = setup.formation_presets;
  prepared.explicit_lineup = setup.lineup.has_value();
  if (setup.lineup) {
    definition.lineup = *setup.lineup;
  } else {
    const auto count = std::min(football::team::kFormationSlots,
                                FormationCount(setup));
    if (count > setup.players.size()) {
      throw std::invalid_argument("formation has no player definition");
    }
    for (std::size_t i = 0; i < setup.players.size(); ++i) {
      if (i < count) definition.lineup.starters[i] = setup.players[i].id;
      else definition.lineup.substitutes.push_back(setup.players[i].id);
    }
  }
  football::team::ValidateTeamDefinition(definition);
  return prepared;
}

}  // namespace football::sim

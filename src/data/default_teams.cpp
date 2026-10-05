#include "data/default_teams.hpp"
#include "data/player_profile.hpp"

namespace football::data {
namespace {

model::Team MakeDefaultTeam(const std::string& name, bool left_team) {
  model::Team team;
  team.name = name;
  constexpr model::PlayerDatabaseId player_ids[] = {398, 11, 254, 320, 103, 188,
                                                    74, 332, 290, 391, 264};
  for (model::PlayerDatabaseId id : player_ids) {
    team.players.push_back(LoadLegacyPlayerProfile(id, left_team));
  }
  return team;
}

}  // namespace

model::Team MakeDefaultHomeTeam() {
  return MakeDefaultTeam("Frequentists United", true);
}

model::Team MakeDefaultAwayTeam() {
  return MakeDefaultTeam("Real Bayesians", false);
}

}  // namespace football::data

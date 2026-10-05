#include "data/default_teams.hpp"
#include "data/player_profile.hpp"

#include <iterator>
#include <utility>

namespace football::data {
namespace {

model::Team MakeDefaultTeam(const std::string& name, bool left_team) {
  model::Team team;
  team.name = name;
  constexpr model::PlayerDatabaseId player_ids[] = {398, 11, 254, 320, 103, 188,
                                                    74, 332, 290, 391, 264};
  // These are identities of the fixture's two distinct rosters, not database
  // keys. Runtime construction never allocates or renumbers model identities.
  model::PlayerId player_id = left_team ? 0 : std::size(player_ids);
  for (model::PlayerDatabaseId database_id : player_ids) {
    model::Player player = LoadLegacyPlayerProfile(database_id, left_team);
    player.id = player_id++;
    team.players.push_back(std::move(player));
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

#include "data/match_setup.hpp"

namespace {

TeamSetup MakeDefaultTeamSetup(const std::string& name) {
  TeamSetup team;
  team.name = name;

  constexpr int kPlayerIds[] = {398, 11, 254, 320, 103, 188,
                                74,  332, 290, 391, 264};
  for (int id : kPlayerIds) {
    team.players.push_back(PlayerSetup{id});
  }
  return team;
}

}  // namespace

MatchSetup MakeDefaultMatchSetup() {
  MatchSetup setup;
  setup.home = MakeDefaultTeamSetup("Frequentists United");
  setup.away = MakeDefaultTeamSetup("Real Bayesians");
  return setup;
}

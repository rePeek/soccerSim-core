#ifndef FOOTBALL_MODEL_TEAM_HPP
#define FOOTBALL_MODEL_TEAM_HPP

#include <string>
#include <vector>

#include "model/formation.hpp"
#include "model/player.hpp"

namespace football::model {

// Static team composition, independent of an opponent, controllers and match
// state. Empty players/formation retain the legacy default roster/formation.
struct Team {
  std::string name;
  std::vector<Player> players;
  Formation formation;
};

namespace detail {
inline Team MakeDefaultTeam(const std::string& name) {
  Team team;
  team.name = name;
  constexpr PlayerDatabaseId player_ids[] = {398, 11, 254, 320, 103, 188,
                                             74, 332, 290, 391, 264};
  for (PlayerDatabaseId id : player_ids) {
    team.players.push_back(Player{id});
  }
  return team;
}
}  // namespace detail

inline Team MakeDefaultHomeTeam() {
  return detail::MakeDefaultTeam("Frequentists United");
}

inline Team MakeDefaultAwayTeam() {
  return detail::MakeDefaultTeam("Real Bayesians");
}

}  // namespace football::model

#endif  // FOOTBALL_MODEL_TEAM_HPP

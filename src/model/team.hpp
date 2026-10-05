#ifndef FOOTBALL_MODEL_TEAM_HPP
#define FOOTBALL_MODEL_TEAM_HPP

#include <string>
#include <vector>

#include "model/formation.hpp"
#include "model/player.hpp"

namespace football::model {

// Static team composition, independent of an opponent, controllers and match
// state. Legacy adapters interpret an empty roster/formation as their defaults.
struct Team {
  std::string name;
  std::vector<Player> players;
  Formation formation;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_TEAM_HPP

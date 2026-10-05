#ifndef FOOTBALL_MODEL_TEAM_HPP
#define FOOTBALL_MODEL_TEAM_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "model/formation.hpp"
#include "model/player.hpp"

namespace football::model {

// A team's role in a match, not a persistent club identity or pitch direction.
// Home/away do not change when play is mirrored or the teams switch ends.
enum class TeamSide : std::uint8_t { Home = 0, Away = 1 };

// Static team composition, independent of an opponent, controllers and match
// state. Legacy adapters interpret an empty roster/formation as their defaults.
struct Team {
  std::string name;
  std::vector<Player> players;
  Formation formation;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_TEAM_HPP

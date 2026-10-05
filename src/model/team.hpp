#ifndef FOOTBALL_MODEL_TEAM_HPP
#define FOOTBALL_MODEL_TEAM_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "model/formation.hpp"
#include "model/player.hpp"

namespace football::model {

// A team's role in a match, not a persistent club identity or pitch direction.
// Home/away do not change when play is mirrored or the teams switch ends.
enum class TeamSide : std::uint8_t { Home = 0, Away = 1 };

// Static team composition, independent of an opponent, controllers and match
// state. Callers supply players explicitly; there is no implicit database roster.
struct Team {
  std::string name;
  std::vector<Player> players;
  Formation formation;
  TacticalFormation tactical_formation;
  // Static tactical preferences, not files, serialized Properties or live AI
  // state. Unspecified preferences leave the simulation's algorithm defaults.
  std::map<std::string, float> tactics;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_TEAM_HPP

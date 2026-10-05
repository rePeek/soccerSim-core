#ifndef FOOTBALL_DATA_DEFAULT_TEAMS_HPP
#define FOOTBALL_DATA_DEFAULT_TEAMS_HPP

#include "model/team.hpp"

namespace football::data {

// Legacy profile-backed defaults, resolved before a GameContext exists.
model::Team MakeDefaultHomeTeam();
model::Team MakeDefaultAwayTeam();

}  // namespace football::data

#endif  // FOOTBALL_DATA_DEFAULT_TEAMS_HPP

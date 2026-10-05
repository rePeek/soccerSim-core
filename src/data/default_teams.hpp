#ifndef FOOTBALL_DATA_DEFAULT_TEAMS_HPP
#define FOOTBALL_DATA_DEFAULT_TEAMS_HPP

#include "model/team.hpp"

namespace football::data {

// Static legacy profile-backed defaults; no environment or simulation is needed.
model::Team MakeDefaultHomeTeam();
model::Team MakeDefaultAwayTeam();

}  // namespace football::data

#endif  // FOOTBALL_DATA_DEFAULT_TEAMS_HPP

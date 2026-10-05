#ifndef FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP
#define FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP

#include "model/team.hpp"

namespace football::app::fixtures {

// CLI/test sample inputs. Never linked into the core shared library.
model::Team MakeDefaultHomeTeam();
model::Team MakeDefaultAwayTeam();

}  // namespace football::app::fixtures

#endif  // FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP

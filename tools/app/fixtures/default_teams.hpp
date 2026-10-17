#ifndef FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP
#define FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP

#include "football/runtime/match_config.hpp"

namespace football::app::fixtures {

// CLI/test sample inputs. Never linked into the core shared library.
football::sim::TeamSetup MakeDefaultHomeTeam();
football::sim::TeamSetup MakeDefaultAwayTeam();

}  // namespace football::app::fixtures

#endif  // FOOTBALL_APP_FIXTURES_DEFAULT_TEAMS_HPP

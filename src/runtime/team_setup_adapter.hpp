#pragma once

#include "football/runtime/match_config.hpp"
#include "football/team/team.hpp"

namespace football::sim {

// Transitional compatibility adapter: TeamSetup remains the external input while
// callers migrate. Runtime immediately converts it to the static team::Team and
// keeps the legacy construction-mode flag privately for initialization only.
struct PreparedTeam {
  football::team::Team team;
  bool explicit_lineup = false;
};

PreparedTeam PrepareTeam(const TeamSetup& setup);

}  // namespace football::sim

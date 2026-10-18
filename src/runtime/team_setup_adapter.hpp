#pragma once

#include "football/runtime/match_config.hpp"
#include "football/team/team.hpp"

namespace football::sim {

// callers migrate. Runtime immediately converts it to the static
// team::TeamDefinition and keeps the legacy construction-mode flag privately
// for initialization only.
struct PreparedTeam {
  football::team::TeamDefinition definition;
  bool explicit_lineup = false;
};

PreparedTeam PrepareTeam(const TeamSetup& setup);

}  // namespace football::sim

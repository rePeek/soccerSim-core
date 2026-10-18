#ifndef FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP
#define FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

#include <vector>

#include "football/player/formation_entry.hpp"
#include "football/runtime/formation.hpp"

namespace football::sim { struct TeamSetup; struct PreparedTeam; }

// Runtime initialisation: turns a team's static configuration into concrete
// per-player formation entries (role adaptation + personal-space
// normalisation). It iterates the whole squad, so it belongs to runtime -- not
// to Player, which only stores its own FormationEntry. The public
// football/runtime/formation.hpp carries only the value types.
std::vector<FormationEntry> BuildFormation(const football::sim::TeamSetup& team);
std::vector<FormationEntry> BuildFormation(const football::sim::PreparedTeam& team);

#endif  // FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

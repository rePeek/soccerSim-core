#ifndef FOOTBALL_SIM_FORMATION_HPP
#define FOOTBALL_SIM_FORMATION_HPP

#include <vector>

#include "sim/runtime/match_config.hpp"
#include "sim/team/formation_entry.hpp"

// Role adaptation and personal-space normalization are simulation algorithms,
// not database lookup. All shape/initial-position values come from the caller.
std::vector<FormationEntry> BuildFormation(const football::sim::TeamSetup& team);

#endif  // FOOTBALL_SIM_FORMATION_HPP

#ifndef FOOTBALL_SIM_FORMATION_HPP
#define FOOTBALL_SIM_FORMATION_HPP

#include <vector>

#include "model/team.hpp"
#include "sim/gamedefines.hpp"

// Role adaptation and personal-space normalization are simulation algorithms,
// not database lookup. All shape/initial-position values come from the caller.
std::vector<FormationEntry> BuildFormation(const football::model::Team& team);

#endif  // FOOTBALL_SIM_FORMATION_HPP

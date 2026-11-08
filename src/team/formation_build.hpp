#ifndef FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP
#define FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

#include <vector>

#include "football/team/formation_entry.hpp"
#include "football/team/team.hpp"

// Runtime initialization: turns a team.s static declaration into concrete
// per-player formation entries (role adaptation + personal-space normalization).
// It iterates the whole squad and is owned by Runtime, which also owns the
// resulting table; Player and Team never cache it.
std::vector<FormationEntry> BuildFormation(const football::team::TeamDefinition& team,
                                           bool explicit_lineup);

#endif  // FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

#ifndef FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP
#define FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

#include <vector>

#include "football/player/state/formation_entry.hpp"
#include "football/team/team.hpp"

// Runtime initialization: turns a team's static declaration into concrete
// per-player formation entries (role adaptation + personal-space normalization).
// It iterates the whole squad, so it belongs to Runtime -- not to Player, which
// only stores its own FormationEntry, and not to Team, which owns actors but
// performs no formation-role adaptation.
std::vector<FormationEntry> BuildFormation(const football::team::TeamDefinition& team,
                                           bool explicit_lineup);

#endif  // FOOTBALL_SIM_RUNTIME_FORMATION_BUILD_HPP

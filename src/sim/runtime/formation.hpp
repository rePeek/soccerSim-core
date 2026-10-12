#ifndef FOOTBALL_SIM_RUNTIME_FORMATION_HPP
#define FOOTBALL_SIM_RUNTIME_FORMATION_HPP

#include <vector>

#include "sim/player/formation_entry.hpp"
#include "sim/player/player_role.hpp"
#include "sim/referee/game_mode.hpp"

namespace football::sim {
struct TeamSetup;

// Initial positions in the environment's public pitch coordinates. Simulation
// converts these to its pitch frame; this is not mutable runtime state.
struct FormationPosition {
  float x = 0.0f;
  float y = 0.0f;
};

struct InitialFormationEntry {
  FormationPosition position;
  e_PlayerRole role = e_PlayerRole_GK;
  bool lazy = false;
  bool controllable = true;
};

using InitialFormation = std::vector<InitialFormationEntry>;

// Static tactical shape, independent of an optional initial-position override.
// Coordinates are normalized pitch fractions (-1..1), not public start poses.
struct TacticalFormationEntry {
  FormationPosition position;
  e_PlayerRole role = e_PlayerRole_GK;
};

using TacticalFormation = std::vector<TacticalFormationEntry>;

}  // namespace football::sim

// Runtime initialisation: turns a team's static configuration into concrete
// per-player formation entries (role adaptation + personal-space
// normalisation). It iterates the whole squad, so it belongs to runtime -- not
// to Player, which only stores its own FormationEntry.
std::vector<FormationEntry> BuildFormation(const football::sim::TeamSetup& team);

#endif  // FOOTBALL_SIM_RUNTIME_FORMATION_HPP

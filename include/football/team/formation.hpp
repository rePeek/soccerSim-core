#pragma once

#include <vector>

#include "football/match/player_role.hpp"

namespace football::team {

// Initial positions in the environment's public pitch coordinates. Runtime
// converts these to its execution frame; the Team owns only the declaration.
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

}  // namespace football::team

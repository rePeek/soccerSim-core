#ifndef FOOTBALL_MODEL_FORMATION_HPP
#define FOOTBALL_MODEL_FORMATION_HPP

#include <vector>

#include "model/football_types.hpp"

namespace football::model {

// Initial positions in the environment's public pitch coordinates. Simulation
// converts these to its pitch frame; this is not mutable runtime state.
struct FormationPosition {
  float x = 0.0f;
  float y = 0.0f;
};

struct FormationEntry {
  FormationPosition position;
  e_PlayerRole role = e_PlayerRole_GK;
  bool lazy = false;
  bool controllable = true;
};

using Formation = std::vector<FormationEntry>;

// Static tactical shape, independent of an optional initial-position override.
// Coordinates are normalized pitch fractions (-1..1), not public start poses.
struct TacticalFormationEntry {
  FormationPosition position;
  e_PlayerRole role = e_PlayerRole_GK;
};

using TacticalFormation = std::vector<TacticalFormationEntry>;

}  // namespace football::model

#endif  // FOOTBALL_MODEL_FORMATION_HPP

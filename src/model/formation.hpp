#ifndef FOOTBALL_MODEL_FORMATION_HPP
#define FOOTBALL_MODEL_FORMATION_HPP

#include <vector>

#include "model/football_types.hpp"

namespace football::model {

// Initial positions in the environment's public pitch coordinates, not the
// simulation's role-adapted positions. Conversion belongs to the env adapter.
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

}  // namespace football::model

#endif  // FOOTBALL_MODEL_FORMATION_HPP

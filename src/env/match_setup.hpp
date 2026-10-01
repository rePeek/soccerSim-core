#ifndef FOOTBALL_DATA_MATCH_SETUP_HPP
#define FOOTBALL_DATA_MATCH_SETUP_HPP

#include <string>
#include <vector>

#include "sim/gamedefines.hpp"
// Static player identity used to construct a match. PlayerData still builds
// its legacy profile at simulation startup; this remains a value type so a
// MatchSetup can be created before a GameContext and its RNG exist.
struct PlayerSetup {
  int database_id = 0;
};

struct TeamSetup {
  std::string name;
  std::vector<PlayerSetup> players;
  std::vector<FormationEntry> formation;
};

// Dimensions are match input. The legacy field geometry is still fixed, so
// non-standard dimensions are not yet supported by the simulation.
struct PitchSetup {
  float length = 105.0f;
  float width = 68.0f;
};

// The complete static composition of one match. Runtime services, rules, and
// simulation state intentionally do not belong here.
struct MatchSetup {
  PitchSetup pitch;
  TeamSetup home;
  TeamSetup away;
};

// Transitional default for legacy callers. New applications should construct
// MatchSetup explicitly.
MatchSetup MakeDefaultMatchSetup();

#endif  // FOOTBALL_DATA_MATCH_SETUP_HPP

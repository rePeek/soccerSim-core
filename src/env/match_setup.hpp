#ifndef FOOTBALL_ENV_MATCH_SETUP_HPP
#define FOOTBALL_ENV_MATCH_SETUP_HPP

#include <string>
#include <vector>

#include "sim/gamedefines.hpp"
#include "data/teamdata.hpp"
#include "sim/pitch.hpp"
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

// The complete static composition of one match. Runtime services, rules, and
// simulation state intentionally do not belong here.
struct MatchSetup {
  Pitch pitch;
  TeamSetup home;
  TeamSetup away;
};

// Transitional default for legacy callers. New applications should construct
// MatchSetup explicitly.
MatchSetup MakeDefaultMatchSetup();

// Adapts a TeamSetup to the inputs TeamData consumes. This lives in env/, the
// composition layer, so that data/ does not depend on the environment types.
TeamCreationData ToTeamCreationData(const TeamSetup& team, int database_id);

#endif  // FOOTBALL_ENV_MATCH_SETUP_HPP

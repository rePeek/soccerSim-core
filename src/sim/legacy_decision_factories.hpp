#ifndef FOOTBALL_SIM_LEGACY_DECISION_FACTORIES_HPP
#define FOOTBALL_SIM_LEGACY_DECISION_FACTORIES_HPP

#include <memory>

class LegacyPlayerDecisionFactory;
class LegacyTeamDecisionFactory;

// The decision implementations a match should use. The composition root picks
// them; Simulation only forwards them to Match. Simulation must never choose a
// default implementation itself, otherwise the domain dependency sim -> ai
// would come back.
struct LegacyDecisionFactories {
  std::shared_ptr<const LegacyPlayerDecisionFactory> player;
  std::shared_ptr<const LegacyTeamDecisionFactory> team;
};

#endif  // FOOTBALL_SIM_LEGACY_DECISION_FACTORIES_HPP

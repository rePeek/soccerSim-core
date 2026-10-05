#ifndef FOOTBALL_SIM_LEGACY_PLAYER_DECISION_FACTORY_HPP
#define FOOTBALL_SIM_LEGACY_PLAYER_DECISION_FACTORY_HPP

#include <memory>

class LegacyPlayerDecision;
class Match;

// TRANSITIONAL SEAM, not a public API. The simulation asks for a decision owner
// through this interface instead of naming a concrete implementation, so the
// dependency arrow points ai -> sim. The default implementation will be wired
// at the composition root once the decision code lives outside simulation.
class LegacyPlayerDecisionFactory {
 public:
  virtual ~LegacyPlayerDecisionFactory() = default;

  virtual std::unique_ptr<LegacyPlayerDecision> Create(Match& match,
                                                      bool lazy) const = 0;
};

#endif  // FOOTBALL_SIM_LEGACY_PLAYER_DECISION_FACTORY_HPP

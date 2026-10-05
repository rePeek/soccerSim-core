#ifndef FOOTBALL_SIM_LEGACY_TEAM_DECISION_FACTORY_HPP
#define FOOTBALL_SIM_LEGACY_TEAM_DECISION_FACTORY_HPP

#include <memory>

class LegacyTeamDecision;
class Team;

// TRANSITIONAL SEAM, not a public API. Teams ask for their decision owner
// through this interface instead of naming a concrete implementation, so the
// dependency arrow points ai -> sim.
class LegacyTeamDecisionFactory {
 public:
  virtual ~LegacyTeamDecisionFactory() = default;

  virtual std::unique_ptr<LegacyTeamDecision> Create(Team& team) const = 0;
};

#endif  // FOOTBALL_SIM_LEGACY_TEAM_DECISION_FACTORY_HPP

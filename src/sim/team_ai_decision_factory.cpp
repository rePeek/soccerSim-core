#include "sim/team_ai_decision_factory.hpp"

#include <memory>

#include "sim/teamAIcontroller.hpp"

namespace {

class TeamAiDecisionFactory final : public LegacyTeamDecisionFactory {
 public:
  std::unique_ptr<LegacyTeamDecision> Create(Team& team) const override {
    return std::make_unique<TeamAIController>(&team);
  }
};

}  // namespace

std::shared_ptr<const LegacyTeamDecisionFactory> MakeTeamAiDecisionFactory() {
  static const std::shared_ptr<const TeamAiDecisionFactory> factory =
      std::make_shared<const TeamAiDecisionFactory>();
  return factory;
}

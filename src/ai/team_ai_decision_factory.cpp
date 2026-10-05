#include "ai/team_ai_decision_factory.hpp"

#include <memory>

#include "ai/team_ai_controller.hpp"

namespace {

class TeamAiDecisionFactory final : public LegacyTeamDecisionFactory {
 public:
  std::unique_ptr<LegacyTeamDecision> Create(Team& team) const override {
    return std::make_unique<TeamAIController>(&team);
  }
};

}  // namespace

std::shared_ptr<const LegacyTeamDecisionFactory>
CreateDefaultTeamAIDecisionFactory() {
  static const std::shared_ptr<const TeamAiDecisionFactory> factory =
      std::make_shared<const TeamAiDecisionFactory>();
  return factory;
}

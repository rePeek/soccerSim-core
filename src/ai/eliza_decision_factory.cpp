#include "ai/eliza_decision_factory.hpp"

#include <memory>

#include "ai/eliza_controller.hpp"

namespace {

class ElizaPlayerDecisionFactory final : public LegacyPlayerDecisionFactory {
 public:
  std::unique_ptr<LegacyPlayerDecision> Create(Match& match,
                                              bool lazy) const override {
    return std::make_unique<ElizaController>(&match, lazy);
  }
};

}  // namespace

std::shared_ptr<const LegacyPlayerDecisionFactory>
CreateDefaultElizaDecisionFactory() {
  static const std::shared_ptr<const ElizaPlayerDecisionFactory> factory =
      std::make_shared<const ElizaPlayerDecisionFactory>();
  return factory;
}

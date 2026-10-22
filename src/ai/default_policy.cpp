#include "default_policy.hpp"

#include "default_decisions.hpp"

namespace football::ai {

void DefaultPolicy::Update(const PlayerAIContext& context,
                           PlayerControlSet& output) {
  if (context.world == nullptr || context.tactics.size() < 2) {
    output.Clear();
    return;
  }
  ComputeDefaultControls(*context.world, context.tactics, output);
}

}  // namespace football::ai

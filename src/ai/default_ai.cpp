#include "football/ai/default_ai.hpp"

#include "default_decisions.hpp"

namespace football::ai {

// The public baseline delegates to the one shared decision body, so DefaultAI
// and the internal DefaultPolicy adapter cannot drift apart.
void DefaultAI::Update(const WorldState &world, PlayerControlSet &output) const {
  ComputeDefaultControls(world, boards_, output);
}

}  // namespace football::ai

#include "default_policy.hpp"

#include "default_decisions.hpp"

namespace football::ai {

void DefaultPolicy::Update(const WorldState& world,
                           std::span<const TacticalBoard, 2> tactics,
                           PlayerControlSet& output) {
  ComputeDefaultControls(world, tactics, output);
}

}  // namespace football::ai

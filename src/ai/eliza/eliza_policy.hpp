#ifndef FOOTBALL_AI_ELIZA_POLICY_HPP
#define FOOTBALL_AI_ELIZA_POLICY_HPP

#include <unordered_map>

#include "eliza_parameters.hpp"
#include "football/ai/player_policy.hpp"
#include "football/player/player_definition.hpp"

namespace football::ai {

// The Eliza heuristic Player policy: role-based off-ball movement, on-ball
// action intent and goalkeeper logic, built only on WorldState + TacticalBoard.
// It owns no actors, match state or RNG; every tick it fully replaces its
// PlayerControlSet output.
//
// It does own per-PlayerId memory (possession time). The memory is keyed by
// identity, never by a pointer or a roster index, and Reset() clears it.
class ElizaPolicy final : public IPlayerPolicy {
 public:
  // Clears the per-player memory; the next Update behaves like a fresh policy.
  void Reset() override;
  void Update(const WorldState& world,
              std::span<const TacticalBoard, 2> tactics,
              PlayerControlSet& output) override;

  // Test/diagnostic view of the policy-owned possession clock.
  int PossessionTicks(football::player::PlayerId player) const;

 private:
  eliza::ElizaParameters parameters_;
  std::unordered_map<football::player::PlayerId, int> possession_ticks_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_ELIZA_POLICY_HPP

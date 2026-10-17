#ifndef FOOTBALL_AI_ELIZA_POLICY_HPP
#define FOOTBALL_AI_ELIZA_POLICY_HPP

#include "eliza_parameters.hpp"
#include "football/ai/player_policy.hpp"

namespace football::ai {

// The Eliza heuristic Player policy: role-based off-ball movement, on-ball
// action intent and goalkeeper logic, built only on WorldState + TacticalBoard.
// It owns no actors, match state or RNG; every tick it fully replaces its
// PlayerControlSet output.
class ElizaPolicy final : public IPlayerPolicy {
 public:
  void Reset() override {}
  void Update(const WorldState& world,
              std::span<const TacticalBoard, 2> tactics,
              PlayerControlSet& output) override;

 private:
  eliza::ElizaParameters parameters_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_ELIZA_POLICY_HPP

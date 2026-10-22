#ifndef FOOTBALL_AI_HEURISTIC_POLICY_HPP
#define FOOTBALL_AI_HEURISTIC_POLICY_HPP

#include <array>
#include <unordered_map>
#include <vector>

#include "football/ai/player_policy.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/player/player_definition.hpp"

namespace football::ai {

// One player's coordinated task for the tick. Diagnostic view for tests and
// tooling, not an output contract.
struct HeuristicAssignment {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  football::math::Vector3 target = football::math::Vector3(0);
  bool holder = false;
  bool presser = false;
  bool cover = false;
  bool marking = false;
  bool rest_defence = false;
  // Bounded decision tendencies after team, role, individual and transient
  // mental inputs. Diagnostic only; the policy output stays the control set.
  float forward_run_bias = 0.5f;
  float pass_risk = 0.5f;
  float press_bias = 0.5f;
};

// The coordinated heuristic Player policy: one shared per-team assignment
// (exclusive chaser/presser, support, rest defence, marking) plus per-player
// on-ball, off-ball and keeper decisions. It owns only per-PlayerId memory and
// never actors, match state or RNG; every tick it fully replaces its output.
//
// It is a new PlayerPolicyKind and never the default. Every tick it reads the
// interpreter projection of the plan plus the observation; it holds no board
// state of its own.
class HeuristicPolicy final : public IPlayerPolicy {
 public:
  void Reset() override;
  void Update(const PlayerAIContext& context,
              PlayerControlSet& output) override;

  // Diagnostics: the coordinated assignment computed by the last Update.
  const std::vector<HeuristicAssignment>& LastAssignment(
      football::sim::TeamSide side) const {
    return assignments_[static_cast<unsigned>(side)];
  }
  int PossessionTicks(football::player::PlayerId player) const;

 private:
  std::array<std::vector<HeuristicAssignment>, 2> assignments_;
  std::unordered_map<football::player::PlayerId, int> possession_ticks_;
  // Per-player smoothed off-ball target, keyed by identity. Cleared by Reset().
  std::unordered_map<football::player::PlayerId, football::math::Vector3>
      smoothed_targets_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_HEURISTIC_POLICY_HPP

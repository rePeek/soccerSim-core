#ifndef FOOTBALL_AI_DEFAULT_POLICY_HPP
#define FOOTBALL_AI_DEFAULT_POLICY_HPP

#include "football/ai/player_policy.hpp"

namespace football::ai {

// The Default IPlayerPolicy. It owns no per-player memory and no boards: the
// AIEngine owns the boards and passes them in, and the decision body is the one
// shared with the public DefaultAI baseline.
class DefaultPolicy final : public IPlayerPolicy {
 public:
  void Update(const PlayerAIContext& context,
              PlayerControlSet& output) override;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_POLICY_HPP

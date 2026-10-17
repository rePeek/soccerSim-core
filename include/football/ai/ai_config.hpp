#ifndef FOOTBALL_AI_AI_CONFIG_HPP
#define FOOTBALL_AI_AI_CONFIG_HPP

#include <array>
#include <optional>

#include "football/ai/player_policy.hpp"
#include "football/ai/tactical_board.hpp"

namespace football::ai {

// Startup policy values only; no runtime handles, callbacks or live intervention.
// Unspecified sides are bootstrapped from the declared models by the policy.
struct AIConfig {
  std::array<std::optional<TacticalBoard>, 2> initial_tactics;
  // Which IPlayerPolicy implementation drives the roster. Default preserves the
  // historical baseline; the value is read once at Start().
  PlayerPolicyKind player_policy = PlayerPolicyKind::Default;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_AI_CONFIG_HPP

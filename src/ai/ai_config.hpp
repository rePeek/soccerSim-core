#ifndef FOOTBALL_AI_AI_CONFIG_HPP
#define FOOTBALL_AI_AI_CONFIG_HPP

#include <array>
#include <optional>

#include "ai/tactical_board.hpp"

namespace football::ai {

// Startup policy values only; no runtime handles, callbacks or live intervention.
// Unspecified sides are bootstrapped from the declared models by the policy.
struct AIConfig {
  std::array<std::optional<TacticalBoard>, 2> initial_tactics;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_AI_CONFIG_HPP

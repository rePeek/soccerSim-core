#ifndef FOOTBALL_AI_AI_CONFIG_HPP
#define FOOTBALL_AI_AI_CONFIG_HPP

#include <array>
#include <optional>

#include "football/ai/tactical_board.hpp"

namespace football::ai {

// Startup values only; no runtime handles, callbacks or live intervention.
// Unspecified sides are bootstrapped from the declared models when the AI
// composition root builds its persistent boards.
struct AIConfig {
  std::array<std::optional<TacticalBoard>, 2> initial_tactics;
  // RFE-6 opt-in autonomous coach loop: ContextCollector -> CoachScheduler ->
  // HeuristicCoachPolicy -> CoachRuntime -> TacticalBoard. Off by default so
  // the existing board-edit-only behavior and determinism are unchanged.
  bool coach_enabled = false;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_AI_CONFIG_HPP

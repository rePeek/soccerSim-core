#pragma once

#include <optional>
#include <span>

#include "football/player/state/player_definition.hpp"

namespace football::team {

// Runtime supplies the current eligibility facts (on field, dismissed, rule
// gates). The Team module only owns the declared priority and deterministic
// selection over those facts; it never reads live Player objects.
struct EligiblePlayer {
  football::player::PlayerId id;
  bool eligible = true;
};

// Explicit fallback policy. None preserves a caller's legacy planner behavior;
// CandidateOrder uses the Runtime-supplied candidate order, never PlayerId
// size or ability ranking.
enum class SelectionFallback { None, CandidateOrder };

std::optional<football::player::PlayerId> SelectEligibleTaker(
    std::span<const football::player::PlayerId> priority,
    std::span<const EligiblePlayer> candidates, SelectionFallback fallback);

}  // namespace football::team

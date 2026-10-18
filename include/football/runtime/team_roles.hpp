#pragma once

#include <optional>
#include <span>
#include <vector>
#include "football/player/player_definition.hpp"

namespace football::sim {
struct TeamLeadership {
  std::optional<football::player::PlayerId> captain;
  std::vector<football::player::PlayerId> vice_captains;
};
struct SetPieceOrders {
  std::vector<football::player::PlayerId> penalties;
  std::vector<football::player::PlayerId> direct_free_kicks;
  std::vector<football::player::PlayerId> indirect_free_kicks;
  std::vector<football::player::PlayerId> corners_left;
  std::vector<football::player::PlayerId> corners_right;
  std::vector<football::player::PlayerId> throw_ins;
};
struct EligiblePlayer {
  football::player::PlayerId id;
  bool eligible = true;  // Runtime combines on-field, dismissal and rule gates.
};
// Explicit fallback: None lets the caller preserve its legacy planner; candidate
// order is supplied by Runtime (e.g. roster declaration order), never ID ranking.
enum class SelectionFallback { None, CandidateOrder };
std::optional<football::player::PlayerId> SelectEligibleTaker(
    std::span<const football::player::PlayerId> priority,
    std::span<const EligiblePlayer> candidates, SelectionFallback fallback);
std::optional<football::player::PlayerId> SelectCaptain(
    const TeamLeadership& leadership, std::span<const EligiblePlayer> candidates,
    SelectionFallback fallback);
}  // namespace football::sim

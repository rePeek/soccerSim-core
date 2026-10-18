#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "football/player/player_definition.hpp"

namespace football::team {

// Formation slot identity, 0..10. Deliberately NOT a PlayerId: a slot keeps its
// meaning when the personnel changes. PlayerId is the independent identity.
using FormationSlotId = std::uint8_t;
inline constexpr std::size_t kFormationSlots = 11;
inline constexpr FormationSlotId kInvalidFormationSlot = 0xFF;

// Static opening squad: at most one PlayerId per slot, plus substitutes.
// Vacancies (fewer than 11 players, later send-offs) are valid and are never
// compressed or renumbered.
struct Lineup {
  std::array<std::optional<football::player::PlayerId>, kFormationSlots> starters{};
  std::vector<football::player::PlayerId> substitutes;
};

}  // namespace football::team

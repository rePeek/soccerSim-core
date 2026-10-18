#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "football/player/player_definition.hpp"

namespace football::sim {
using FormationSlotId = std::uint8_t;
inline constexpr std::size_t kFormationSlots = 11;
inline constexpr FormationSlotId kInvalidFormationSlot = 0xFF;

struct TeamLineup {
  std::array<std::optional<football::player::PlayerId>, kFormationSlots> starters{};
  std::vector<football::player::PlayerId> substitutes;
};

// Runtime owns this value. Vacancies are never compressed. Replacement does not
// touch Team's initial lineup or reusable formation templates.
struct ActiveLineupState {
  std::array<std::optional<football::player::PlayerId>, kFormationSlots> slots{};
};
void VacateSlot(ActiveLineupState& lineup, football::player::PlayerId player);
void ReplaceSlot(ActiveLineupState& lineup, FormationSlotId slot,
                 football::player::PlayerId player);
}  // namespace football::sim

#pragma once

#include <array>
#include <optional>

#include "football/player/player_definition.hpp"
#include "football/team/lineup.hpp"

namespace football::sim {

// Runtime-owned live slot binding for one team. It starts from Team's declared
// Lineup and changes through send-offs/substitutions. Empty slots are preserved,
// never compressed or renumbered. This is match state, not Team configuration.
struct ActiveLineupState {
  std::array<std::optional<football::player::PlayerId>,
             football::team::kFormationSlots> slots{};
};

void VacateSlot(ActiveLineupState& lineup, football::player::PlayerId player);
void ReplaceSlot(ActiveLineupState& lineup, football::team::FormationSlotId slot,
                 football::player::PlayerId player);

}  // namespace football::sim

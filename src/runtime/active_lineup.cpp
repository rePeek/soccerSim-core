#include "football/runtime/active_lineup.hpp"

#include <stdexcept>

namespace football::sim {

void VacateSlot(ActiveLineupState& lineup,
                football::player::PlayerId player) {
  for (auto& id : lineup.slots) {
    if (id == player) id.reset();
  }
}

void ReplaceSlot(ActiveLineupState& lineup,
                 football::team::FormationSlotId slot,
                 football::player::PlayerId player) {
  if (slot >= football::team::kFormationSlots ||
      player == football::player::kInvalidPlayerId) {
    throw std::invalid_argument("invalid slot replacement");
  }
  for (std::size_t i = 0; i < lineup.slots.size(); ++i) {
    if (i != slot && lineup.slots[i] == player) {
      throw std::invalid_argument("replacement already on field");
    }
  }
  lineup.slots[slot] = player;
}

}  // namespace football::sim

#ifndef FOOTBALL_SIM_OBSERVATION_PLAYER_SLOTS_HPP
#define FOOTBALL_SIM_OBSERVATION_PLAYER_SLOTS_HPP

#include <cstddef>
#include <optional>
#include <vector>

#include "model/player.hpp"
#include "model/team.hpp"

namespace football::sim::observation {

// Static slot -> identity/team-side table established when a match starts and
// never compacted or reused for a different identity. It complements
// SnapshotMetadata (which stays pure physical/identity data) with the stable
// player -> TeamSide association the referee needs without holding live Team
// pointers, and survives send-offs and half-time side changes.
struct PlayerSlotTable {
  std::vector<football::model::PlayerId> player_ids;
  std::vector<football::model::TeamSide> team_sides;

  std::size_t Size() const { return player_ids.size(); }

  std::optional<std::size_t> SlotOf(football::model::PlayerId id) const {
    for (std::size_t i = 0; i < player_ids.size(); ++i) {
      if (player_ids[i] == id) return i;
    }
    return std::nullopt;
  }

  std::optional<football::model::TeamSide> SideOf(football::model::PlayerId id) const {
    const auto slot = SlotOf(id);
    if (!slot.has_value()) return std::nullopt;
    return team_sides[*slot];
  }
};

}  // namespace football::sim::observation

#endif  // FOOTBALL_SIM_OBSERVATION_PLAYER_SLOTS_HPP

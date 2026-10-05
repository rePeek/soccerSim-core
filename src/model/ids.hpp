#ifndef FOOTBALL_MODEL_IDS_HPP
#define FOOTBALL_MODEL_IDS_HPP

#include <cstdint>
#include <limits>

namespace football::model {

// Identities used by match snapshots and control contracts. They are local to
// a match and are not the legacy database keys used to resolve player profiles.
using TeamId = std::uint8_t;
using PlayerId = std::uint32_t;

inline constexpr TeamId kInvalidTeamId = std::numeric_limits<TeamId>::max();
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

using PlayerDatabaseId = int;

}  // namespace football::model

#endif  // FOOTBALL_MODEL_IDS_HPP

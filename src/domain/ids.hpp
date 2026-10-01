#ifndef FOOTBALL_DOMAIN_IDS_HPP
#define FOOTBALL_DOMAIN_IDS_HPP

#include <cstdint>
#include <limits>

using TeamId = std::uint8_t;
using PlayerId = std::uint32_t;

inline constexpr TeamId kInvalidTeamId = std::numeric_limits<TeamId>::max();
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

#endif  // FOOTBALL_DOMAIN_IDS_HPP

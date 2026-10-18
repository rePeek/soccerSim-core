// A team's role in a match, not a persistent club identity or pitch direction.
// Neutral match value: actors and rules name it without including a Team/Runtime
// aggregate. Home/away do not change when play is mirrored or ends switch.
#ifndef FOOTBALL_MATCH_TEAM_SIDE_HPP
#define FOOTBALL_MATCH_TEAM_SIDE_HPP

#include <cstdint>

namespace football::sim {

enum class TeamSide : std::uint8_t { Home = 0, Away = 1 };

}  // namespace football::sim

#endif  // FOOTBALL_MATCH_TEAM_SIDE_HPP

// The actor-readable slice of the competition touch record. Runtime owns the
// full TouchState; this base subobject is borrowed live so an accepted touch
// that lands inside the same Process() call is visible immediately.
#ifndef FOOTBALL_PLAYER_PLAYER_TOUCH_FACTS_HPP
#define FOOTBALL_PLAYER_PLAYER_TOUCH_FACTS_HPP

#include <array>

#include "football/player/player_definition.hpp"

namespace football::player {

struct PlayerTouchFacts {
  int last_team = -1;
  std::array<PlayerId, 2> last_player_by_team{
      kInvalidPlayerId, kInvalidPlayerId};
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_TOUCH_FACTS_HPP

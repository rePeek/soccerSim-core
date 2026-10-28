// Player-side touch queries over the borrowed competition touch facts. These
// are the actor's view of "who touched last / how strong is the opponent
// bias"; the global arbitration and touch recording stay in the Event/Runtime
// layer and reach Player only through the read-only facts.
#ifndef FOOTBALL_PLAYER_TOUCH_QUERY_HPP
#define FOOTBALL_PLAYER_TOUCH_QUERY_HPP

#include <span>

#include "football/foundation/time/tick.hpp"
#include "football/player/execution/player_touch_facts.hpp"

class Player;

namespace football::sim::player {

// Identity lookup includes inactive/sent-off entries. The caller supplies the
// team id and its roster slice, so no runtime Team object is needed.
Player* LastTouchPlayer(const football::player::PlayerTouchFacts& touches,
                        int team_id, std::span<Player* const> players);
float TeamTouchBias(const football::player::PlayerTouchFacts& touches,
                    int team_id, std::span<Player* const> players,
                    int decay_ms, Tick now);

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_TOUCH_QUERY_HPP

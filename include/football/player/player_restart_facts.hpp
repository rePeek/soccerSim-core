// Actor-readable restart facts. Runtime owns the full RestartContext; this
// base subobject is borrowed live so a restart transition inside the same
// Process() call is visible. Player never runs rule adjudication from it.
#ifndef FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP
#define FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP

class Player;

namespace football::player {

struct PlayerRestartFacts {
  bool active = false;
  Player* taker = nullptr;
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP

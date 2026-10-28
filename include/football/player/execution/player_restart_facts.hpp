// Actor-readable restart facts. Runtime owns the full RestartContext; this
// base subobject is borrowed live so a restart transition inside the same
// Process() call is visible. Player never runs rule adjudication from it.
#ifndef FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP
#define FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP

#include "football/player/state/player_definition.hpp"

namespace football::player {

struct PlayerRestartFacts {
  bool active = false;
  // Stable identity of the restart taker. Runtime resolves it to the live actor
  // for execution; identity-only consumers read this value directly, so the
  // contract stays snapshot/AI-safe instead of carrying an object reference.
  PlayerId taker = kInvalidPlayerId;
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_RESTART_FACTS_HPP

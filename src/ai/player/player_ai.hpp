#ifndef FOOTBALL_AI_PLAYER_PLAYER_AI_HPP
#define FOOTBALL_AI_PLAYER_PLAYER_AI_HPP

#include <span>

#include "control/player_control_set.hpp"
#include "control/tactical_board.hpp"
#include "state/world_state.hpp"

// Decision algorithm boundary. Implementations may be rule based, learned, or
// remote, while control and simulation remain value-oriented modules.
class PlayerAI {
 public:
  virtual ~PlayerAI() = default;

  virtual void Update(const WorldStateView& world,
                      std::span<const TacticalBoard> tactics,
                      PlayerControlSet& controls) = 0;
};

#endif  // FOOTBALL_AI_PLAYER_PLAYER_AI_HPP

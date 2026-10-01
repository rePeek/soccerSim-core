#ifndef FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_HPP
#define FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_HPP

#include "control/player/player_control_context.hpp"
#include "control/player/player_intent.hpp"

class PlayerControl {
 public:
  virtual ~PlayerControl() = default;

  virtual PlayerIntent Decide(const PlayerControlContext& context) = 0;
  virtual void Reset() {}
};

#endif  // FOOTBALL_CONTROL_PLAYER_PLAYER_CONTROL_HPP

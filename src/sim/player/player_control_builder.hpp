#ifndef FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

#include "sim/player/player_control.hpp"
#include "sim/player/player_command.hpp"

class Player;
class Ball;
struct RefereeBuffer;
namespace football::model { struct Pitch; }
namespace football::sim::event { struct TouchState; }

// Only the inputs consumed by command authorization/keeper-hands legality.
// Borrowed for one construction call, never retained by Player.
struct PlayerCommandInputs {
  const Ball& ball;
  const football::sim::event::TouchState& touches;
  const RefereeBuffer& restart;
  const Player* retainer;
  const football::model::Pitch& pitch;
};

// Converts decision-domain values into the existing execution command format.
// This is the only intentional control-to-simulation translation boundary.
PlayerCommandQueue BuildPlayerCommands(const PlayerControl& control,
                                       Player& player, const PlayerCommandInputs& inputs);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

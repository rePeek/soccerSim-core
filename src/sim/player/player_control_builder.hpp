#ifndef FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

#include "football/player/player_control.hpp"
#include "football/pitch/pitch.hpp"
#include "sim/player/player_command.hpp"
#include "sim/player/player_team_view.hpp"

class Player;
namespace football::ball { class Ball; }
struct RefereeBuffer;
namespace football::pitch { class Pitch; }
namespace football::sim::event { struct TouchState; }

// Only the inputs consumed by command authorization/keeper-hands legality.
// Borrowed for one construction call, never retained by Player.
struct PlayerCommandInputs {
  const football::ball::Ball& ball;
  const football::sim::event::TouchState& touches;
  const RefereeBuffer& restart;
  const Player* retainer;
  const football::pitch::Pitch& pitch;
  // The acting player's own roster slice and runtime team facts. Borrowed.
  const PlayerTeamView& own_team;
};

// Converts decision-domain values into the existing execution command format.
// This is the only intentional control-to-simulation translation boundary.
PlayerCommandQueue BuildPlayerCommands(const PlayerControl& control,
                                       Player& player, const PlayerCommandInputs& inputs);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

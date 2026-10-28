#ifndef FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

#include "football/player/player_control.hpp"
#include "football/pitch/pitch.hpp"
#include "player/execution/player_authorization.hpp"
#include "player/execution/player_command.hpp"
#include "football/player/player_command_status.hpp"
#include "player/execution/player_team_view.hpp"

class Player;
namespace football::ball { class Ball; }
namespace football::pitch { class Pitch; }

// Only the inputs consumed by command authorization/keeper-hands legality.
// Borrowed for one construction call, never retained by Player. Rule decisions
// arrive through the Player-owned authorization port; Player names no Referee,
// TouchState or RestartContext type.
struct PlayerCommandInputs {
  const football::ball::Ball& ball;
  const Player* retainer;
  const football::pitch::Pitch& pitch;
  // The acting player's own roster slice and runtime team facts. Borrowed.
  const PlayerTeamView& own_team;
  // Runtime-owned rule adapter. Non-null in a match; a null port applies the
  // permissive default (no restart gate, all actions allowed).
  const football::sim::player::PlayerAuthorizationPort* authorization = nullptr;
};

// The conversion result. A discrete action is only successful when status is
// Accepted; a movement fallback keeps locomotion continuity but is not the
// requested action.
struct CommandBuildResult {
  football::sim::player::CommandBuildStatus status =
      football::sim::player::CommandBuildStatus::MovementOnly;
  PlayerCommandQueue commands;
};

// Converts decision-domain values into the existing execution command format.
// This is the only intentional control-to-simulation translation boundary.
CommandBuildResult BuildPlayerCommands(const PlayerControl& control,
                                       Player& player,
                                       const PlayerCommandInputs& inputs);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

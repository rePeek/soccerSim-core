#ifndef FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

#include "control/player_control.hpp"
#include "sim/gamedefines.hpp"

class PlayerBase;

// Converts decision-domain values into the existing execution command format.
// This is the only intentional control-to-simulation translation boundary.
PlayerCommandQueue BuildPlayerCommands(const PlayerControl& control,
                                       const PlayerBase& player);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_CONTROL_BUILDER_HPP

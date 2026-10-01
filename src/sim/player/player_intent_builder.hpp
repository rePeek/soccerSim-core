#ifndef FOOTBALL_SIM_PLAYER_PLAYER_INTENT_BUILDER_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_INTENT_BUILDER_HPP

#include "control/player/player_intent.hpp"
#include "sim/gamedefines.hpp"

class PlayerBase;

// Converts decision-domain values into the existing execution command format.
// This is the only intentional control-to-simulation translation boundary.
PlayerCommandQueue BuildPlayerCommands(const PlayerIntent& intent,
                                       const PlayerBase& player);

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_INTENT_BUILDER_HPP

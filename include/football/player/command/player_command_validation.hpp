#ifndef FOOTBALL_PLAYER_COMMAND_VALIDATION_HPP
#define FOOTBALL_PLAYER_COMMAND_VALIDATION_HPP

#include "football/player/command/player_control.hpp"


namespace football::sim::player {

// Pure Command-contract checks. Live actor/ball availability is resolved later
// by the Execution adapter; these only answer whether the supplied parameters
// are sufficient to attempt the action.
inline bool ControlActionTargetIsExplicit(const PlayerControl& control) {
  const bool pass = control.action == ControlAction::ShortPass ||
      control.action == ControlAction::LongPass ||
      control.action == ControlAction::ThroughPass ||
      control.action == ControlAction::HighPass;
  return !pass ||
         control.target_player.has_value() ||
         control.target_position.has_value();
}

inline bool ControlActionAimIsExplicit(const PlayerControl& control) {
  return control.action != ControlAction::Shoot ||
         control.target_position.has_value() ||
         control.move_direction.GetLength() > 0.000001f;
}

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_COMMAND_VALIDATION_HPP

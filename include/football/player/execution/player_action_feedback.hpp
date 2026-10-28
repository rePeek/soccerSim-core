#ifndef FOOTBALL_SIM_PLAYER_ACTION_FEEDBACK_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_FEEDBACK_HPP

#include "football/animation/types.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/command/player_control.hpp"
#include "football/player/state/player_command_status.hpp"

// Execution feedback for the last discrete Command attempt. Command ->
// animation is not one-to-one, so both the source Command and the concrete
// e_FunctionType are exposed. A failed build leaves installed=false and
// carries command_status; `interrupted` means a new action replaced an
// incomplete one (a natural last-frame switch is not an interruption).
struct PlayerActionFeedback {
  ControlAction source_action = ControlAction::None;
  football::sim::player::CommandBuildStatus command_status =
      football::sim::player::CommandBuildStatus::MovementOnly;
  e_FunctionType type = e_FunctionType_None;
  bool installed = false;
  bool contact_due = false;
  bool touch_prepared = false;
  bool touch_accepted = false;
  bool completed = false;
  bool interrupted = false;
  int start_frame = -1;
  int touch_frame = -1;
  football::sim::Tick prepared_touch_tick{};
};

#endif  // FOOTBALL_SIM_PLAYER_ACTION_FEEDBACK_HPP

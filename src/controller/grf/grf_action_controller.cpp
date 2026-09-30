#include "controller/grf/grf_action_controller.hpp"

#include <cstdlib>

namespace {

[[noreturn]] void InvalidAction() {
  std::abort();
}

}  // namespace

bool GrfActionController::IsStickyActionActive(
    Action action, ExternalController& controller) {
  switch (action) {
    case game_left:
      return controller.GetOriginalDirection() == blunted::Vector3(-1, 0, 0);
    case game_top_left:
      return controller.GetOriginalDirection() == blunted::Vector3(-1, 1, 0);
    case game_top:
      return controller.GetOriginalDirection() == blunted::Vector3(0, 1, 0);
    case game_top_right:
      return controller.GetOriginalDirection() == blunted::Vector3(1, 1, 0);
    case game_right:
      return controller.GetOriginalDirection() == blunted::Vector3(1, 0, 0);
    case game_bottom_right:
      return controller.GetOriginalDirection() == blunted::Vector3(1, -1, 0);
    case game_bottom:
      return controller.GetOriginalDirection() == blunted::Vector3(0, -1, 0);
    case game_bottom_left:
      return controller.GetOriginalDirection() == blunted::Vector3(-1, -1, 0);
    case game_keeper_rush:
      return controller.GetButton(e_ButtonFunction_KeeperRush);
    case game_pressure:
      return controller.GetButton(e_ButtonFunction_Pressure);
    case game_team_pressure:
      return controller.GetButton(e_ButtonFunction_TeamPressure);
    case game_sprint:
      return controller.GetButton(e_ButtonFunction_Sprint);
    case game_dribble:
      return controller.GetButton(e_ButtonFunction_Dribble);
    default:
      InvalidAction();
  }
}

void GrfActionController::Apply(Action action, ExternalController& controller) {
  controller.SetDisabled(false);
  switch (action) {
    case game_idle:
      return;
    case game_left:
      return controller.SetDirection(blunted::Vector3(-1, 0, 0));
    case game_top_left:
      return controller.SetDirection(blunted::Vector3(-1, 1, 0));
    case game_top:
      return controller.SetDirection(blunted::Vector3(0, 1, 0));
    case game_top_right:
      return controller.SetDirection(blunted::Vector3(1, 1, 0));
    case game_right:
      return controller.SetDirection(blunted::Vector3(1, 0, 0));
    case game_bottom_right:
      return controller.SetDirection(blunted::Vector3(1, -1, 0));
    case game_bottom:
      return controller.SetDirection(blunted::Vector3(0, -1, 0));
    case game_bottom_left:
      return controller.SetDirection(blunted::Vector3(-1, -1, 0));
    case game_long_pass:
      return controller.SetButton(e_ButtonFunction_LongPass, true);
    case game_high_pass:
      return controller.SetButton(e_ButtonFunction_HighPass, true);
    case game_short_pass:
      return controller.SetButton(e_ButtonFunction_ShortPass, true);
    case game_shot:
      return controller.SetButton(e_ButtonFunction_Shot, true);
    case game_keeper_rush:
      return controller.SetButton(e_ButtonFunction_KeeperRush, true);
    case game_sliding:
      return controller.SetButton(e_ButtonFunction_Sliding, true);
    case game_pressure:
      return controller.SetButton(e_ButtonFunction_Pressure, true);
    case game_team_pressure:
      return controller.SetButton(e_ButtonFunction_TeamPressure, true);
    case game_switch:
      return controller.SetButton(e_ButtonFunction_Switch, true);
    case game_sprint:
      return controller.SetButton(e_ButtonFunction_Sprint, true);
    case game_dribble:
      return controller.SetButton(e_ButtonFunction_Dribble, true);
    case game_release_direction:
      return controller.SetDirection(blunted::Vector3(0, 0, 0));
    case game_release_long_pass:
      return controller.SetButton(e_ButtonFunction_LongPass, false);
    case game_release_high_pass:
      return controller.SetButton(e_ButtonFunction_HighPass, false);
    case game_release_short_pass:
      return controller.SetButton(e_ButtonFunction_ShortPass, false);
    case game_release_shot:
      return controller.SetButton(e_ButtonFunction_Shot, false);
    case game_release_keeper_rush:
      return controller.SetButton(e_ButtonFunction_KeeperRush, false);
    case game_release_sliding:
      return controller.SetButton(e_ButtonFunction_Sliding, false);
    case game_release_pressure:
      return controller.SetButton(e_ButtonFunction_Pressure, false);
    case game_release_team_pressure:
      return controller.SetButton(e_ButtonFunction_TeamPressure, false);
    case game_release_switch:
      return controller.SetButton(e_ButtonFunction_Switch, false);
    case game_release_sprint:
      return controller.SetButton(e_ButtonFunction_Sprint, false);
    case game_release_dribble:
      return controller.SetButton(e_ButtonFunction_Dribble, false);
    case game_builtin_ai:
      return controller.SetDisabled(true);
  }
  InvalidAction();
}

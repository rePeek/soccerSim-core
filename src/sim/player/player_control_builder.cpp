#include "sim/player/player_control_builder.hpp"

#include "sim/player/playerbase.hpp"

namespace {
e_FunctionType ToFunctionType(ControlAction action) {
  switch (action) {
    case ControlAction::ShortPass: return e_FunctionType_ShortPass;
    case ControlAction::LongPass: return e_FunctionType_LongPass;
    case ControlAction::HighPass: return e_FunctionType_HighPass;
    case ControlAction::Shoot: return e_FunctionType_Shot;
    case ControlAction::Tackle: return e_FunctionType_Sliding;
    case ControlAction::None: return e_FunctionType_Movement;
  }
  return e_FunctionType_Movement;
}
}  // namespace

PlayerCommandQueue BuildPlayerCommands(const PlayerControl& control,
                                       const PlayerBase& player) {
  PlayerCommand command;
  command.desiredFunctionType = ToFunctionType(control.action);
  command.useDesiredMovement = true;
  command.desiredDirection =
      control.move_direction.GetNormalized(player.GetDirectionVec());
  command.desiredVelocityFloat = std::max(0.0f, control.desired_speed);
  if (control.look_at) {
    command.useDesiredLookAt = true;
    command.desiredLookAt = *control.look_at;
  }
  if (control.action != ControlAction::None) {
    command.useTouchInfo = true;
    command.touchInfo.inputDirection = command.desiredDirection;
    command.touchInfo.inputPower = std::max(0.0f, control.power);
    command.touchInfo.autoDirectionBias =
        control.target_position ? 0.0f : 1.0f;
    command.touchInfo.autoPowerBias = control.power > 0.0f ? 0.0f : 1.0f;
    if (control.target_position) {
      command.touchInfo.desiredDirection =
          (*control.target_position - player.GetPosition())
              .GetNormalized(command.desiredDirection);
      command.touchInfo.desiredPower = control.power;
    }
  }
  return PlayerCommandQueue{command};
}

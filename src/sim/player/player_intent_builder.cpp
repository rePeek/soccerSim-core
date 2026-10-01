#include "sim/player/player_intent_builder.hpp"

#include "sim/player/playerbase.hpp"

namespace {
e_FunctionType ToFunctionType(IntentAction action) {
  switch (action) {
    case IntentAction::ShortPass: return e_FunctionType_ShortPass;
    case IntentAction::LongPass: return e_FunctionType_LongPass;
    case IntentAction::HighPass: return e_FunctionType_HighPass;
    case IntentAction::Shoot: return e_FunctionType_Shot;
    case IntentAction::Tackle: return e_FunctionType_Sliding;
    case IntentAction::None: return e_FunctionType_Movement;
  }
  return e_FunctionType_Movement;
}
}  // namespace

PlayerCommandQueue BuildPlayerCommands(const PlayerIntent& intent,
                                       const PlayerBase& player) {
  PlayerCommand command;
  command.desiredFunctionType = ToFunctionType(intent.action);
  command.useDesiredMovement = true;
  command.desiredDirection = intent.move_direction.GetNormalized(player.GetDirectionVec());
  command.desiredVelocityFloat = std::max(0.0f, intent.desired_speed);
  if (intent.look_at) {
    command.useDesiredLookAt = true;
    command.desiredLookAt = *intent.look_at;
  }
  if (intent.action != IntentAction::None) {
    command.useTouchInfo = true;
    command.touchInfo.inputDirection = command.desiredDirection;
    command.touchInfo.inputPower = std::max(0.0f, intent.power);
    command.touchInfo.autoDirectionBias = intent.target_position ? 0.0f : 1.0f;
    command.touchInfo.autoPowerBias = intent.power > 0.0f ? 0.0f : 1.0f;
    if (intent.target_position) {
      command.touchInfo.desiredDirection =
          (*intent.target_position - player.GetPosition()).GetNormalized(command.desiredDirection);
      command.touchInfo.desiredPower = intent.power;
    }
  }
  return PlayerCommandQueue{command};
}

#include "player/player_control_builder.hpp"

#include <algorithm>
#include <cmath>

#include "football/ball/ball.hpp"
#include "football/pitch/pitch.hpp"
#include "football/pitch/pitch_frame.hpp"
#include "player/player.hpp"
#include "player/kick_targeting.hpp"

namespace {
e_FunctionType ToFunctionType(ControlAction action) {
  switch (action) {
    case ControlAction::ShortPass: return e_FunctionType_ShortPass;
    case ControlAction::LongPass: return e_FunctionType_LongPass;
    case ControlAction::HighPass: return e_FunctionType_HighPass;
    case ControlAction::Shoot: return e_FunctionType_Shot;
    case ControlAction::Tackle: return e_FunctionType_Sliding;
    case ControlAction::Dribble: return e_FunctionType_BallControl;
    case ControlAction::Trap: return e_FunctionType_Trap;
    case ControlAction::Save: return e_FunctionType_Deflect;
    case ControlAction::None: return e_FunctionType_Movement;
  }
  return e_FunctionType_Movement;
}
}  // namespace

PlayerCommandQueue BuildPlayerCommands(const PlayerControl& input,
                                       Player& player, const PlayerCommandInputs& inputs) {
  // Value controls use home pitch coordinates; convert at consumption, after
  // Simulation selects the actor's processing frame. Never mutate caller data.
  PlayerControl control = input;
  const auto runtime_frame = FromHomePitchFrame(player.GetTeamSide() == football::sim::TeamSide::Home, player.GetExecutionDirection());
  control.move_direction = runtime_frame.Direction(control.move_direction);
  if (control.look_at) control.look_at = runtime_frame.Position(*control.look_at);
  if (control.target_position) control.target_position = runtime_frame.Position(*control.target_position);
  PlayerCommand movement;
  movement.desiredFunctionType = e_FunctionType_Movement;
  movement.useDesiredMovement = true;
  movement.desiredDirection = control.move_direction.Get2D().GetNormalized(player.GetDirectionVec());
  movement.desiredVelocityFloat = std::max(0.f, control.desired_speed);
  if (control.look_at) {
    movement.useDesiredLookAt = true;
    movement.desiredLookAt = control.look_at->Get2D();
  }
  // Rules own restart and keeper-hands legality. Build a value-only request
  // and let the Runtime-owned authorization port sample its live rule facts
  // here, where the original checks ran; Player never adjudicates.
  football::sim::player::PlayerAuthorizationRequest authorization_request;
  authorization_request.actor = &player;
  authorization_request.team_id = inputs.own_team.team_id;
  authorization_request.teammates = inputs.own_team.teammates;
  authorization_request.save_request = control.action == ControlAction::Save;
  if (authorization_request.save_request) {
    authorization_request.is_goalkeeper = inputs.own_team.goalie == &player;
    authorization_request.ball_retained = inputs.retainer != nullptr;
    authorization_request.predicted_ball =
        inputs.ball.Predict(football::sim::TickSpan{16}).position;
    authorization_request.half_length = inputs.pitch.half_length();
    authorization_request.execution_direction = player.GetExecutionDirection();
  }
  football::sim::player::PlayerActionAuthorization authorization;
  if (inputs.authorization != nullptr) {
    authorization = inputs.authorization->Evaluate(authorization_request);
  }
  if (authorization.restart_gate && !authorization.action_allowed) {
    if (!authorization.movement_allowed) movement.desiredVelocityFloat = 0.f;
    return {movement};
  }
  if (control.action == ControlAction::None) return {movement};

  PlayerCommand command = movement;
  command.desiredFunctionType = ToFunctionType(control.action);
  if (control.action == ControlAction::ShortPass || control.action == ControlAction::LongPass ||
      control.action == ControlAction::HighPass || control.action == ControlAction::Shoot) {
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    command.useTouchInfo = true;
    command.touchInfo.inputDirection = command.desiredDirection;
    command.touchInfo.inputPower = std::max(0.f, control.power);
    command.touchInfo.autoDirectionBias = control.target_position ? 0.f : 1.f;
    command.touchInfo.autoPowerBias = control.power > 0.f ? 0.f : 1.f;
    if (control.target_player) {
      for (Player *candidate : inputs.own_team.teammates) {
        if (candidate->IsActive() && candidate->GetID() == *control.target_player &&
            candidate != &player) {
          command.touchInfo.targetPlayer = candidate;
          command.touchInfo.forcedTargetPlayer = candidate;
          break;
        }
      }
    }
    if (control.target_position) {
      command.touchInfo.desiredDirection = (*control.target_position - player.GetPosition())
          .GetNormalized(command.desiredDirection);
      command.touchInfo.desiredPower = control.power;
      command.touchInfo.inputDirection = command.touchInfo.desiredDirection;
    }
    if (control.action == ControlAction::Shoot) {
      command.touchInfo.desiredDirection = football::sim::mechanics::GetShotDirection(
          &player, inputs.pitch, command.touchInfo.inputDirection, command.touchInfo.autoDirectionBias);
      command.touchInfo.desiredPower = control.power;
    } else {
      football::sim::mechanics::GetPass(&player, inputs.own_team.teammates, command.desiredFunctionType,
          command.touchInfo.inputDirection, command.touchInfo.inputPower,
          command.touchInfo.autoDirectionBias, command.touchInfo.autoPowerBias,
          command.touchInfo.desiredDirection, command.touchInfo.desiredPower,
          command.touchInfo.targetPlayer, command.touchInfo.forcedTargetPlayer);
    }
  }
  if (control.action == ControlAction::Save) {
    // Hands legality is a Rules decision; a rejected Save still publishes the
    // movement intent.
    if (!authorization.goalkeeper_hands_allowed) return {movement};
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
  }
  // Mechanical movement fallback, not a second AI decision. An unavailable
  // action must still publish a fresh locomotion intent on reset/re-entry.
  return {command, movement};
}

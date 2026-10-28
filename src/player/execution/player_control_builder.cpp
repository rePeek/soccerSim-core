#include "player/execution/player_control_builder.hpp"

#include <algorithm>

#include "football/ball/ball.hpp"
#include "football/pitch/pitch.hpp"
#include "player/player.hpp"
#include "player/command/player_action_mapping.hpp"
#include "player/execution/kick_targeting.hpp"


CommandBuildResult BuildPlayerCommands(const PlayerControl& input,
                                       Player& player,
                                       const PlayerCommandInputs& inputs) {
  using football::sim::player::CommandBuildStatus;
  // Controls are already in the single Stadium Frame; never mutate caller data.
  PlayerControl control = input;
  PlayerCommand movement;
  movement.desiredFunctionType = e_FunctionType_Movement;
  movement.useDesiredMovement = true;
  movement.desiredDirection =
      control.move_direction.Get2D().GetNormalized(player.GetDirectionVec());
  movement.desiredVelocityFloat = std::max(0.f, control.desired_speed);
  if (control.look_at) {
    movement.useDesiredLookAt = true;
    movement.desiredLookAt = control.look_at->Get2D();
  }
  const auto result = [](CommandBuildStatus status,
                         PlayerCommandQueue commands) {
    return CommandBuildResult{status, std::move(commands)};
  };
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
    authorization_request.defending_direction = player.GetDefendingDirection();
  }
  football::sim::player::PlayerActionAuthorization authorization;
  if (inputs.authorization != nullptr) {
    authorization = inputs.authorization->Evaluate(authorization_request);
  }
  if (authorization.restart_gate && !authorization.action_allowed) {
    if (!authorization.movement_allowed) movement.desiredVelocityFloat = 0.f;
    return result(CommandBuildStatus::Rejected, {movement});
  }
  if (control.action == ControlAction::None)
    return result(CommandBuildStatus::MovementOnly, {movement});
  if (control.action == ControlAction::Pull ||
      control.action == ControlAction::CallSupport) {
    // Defined Commands with no current execution path: Pull has no correct
    // clip, and CallSupport must be resolved by Team/Planner into another
    // player's Move Command. Never substitute an unrelated action.
    return result(CommandBuildStatus::Unsupported, {movement});
  }

  const bool pass_action =
      football::sim::player::IsPassControlAction(control.action);
  if (pass_action && !control.target_player && !control.target_position) {
    // A pass must name its receiver or its target point.
    return result(CommandBuildStatus::Rejected, {movement});
  }
  if (control.action == ControlAction::Shoot && !control.target_position &&
      control.move_direction.GetLength() <= 0.000001f) {
    // A shot needs an explicit aim direction or target point.
    return result(CommandBuildStatus::Rejected, {movement});
  }

  PlayerCommand command = movement;
  command.source_action = control.action;
  const float target_distance =
      control.target_position
          ? (*control.target_position - player.GetPosition()).GetLength()
          : 0.0f;
  command.desiredFunctionType =
      football::sim::player::ControlActionFunctionType(control.action,
                                                       target_distance);
  if (pass_action || control.action == ControlAction::Shoot) {
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    command.useTouchInfo = true;
    if (control.target_player) {
      for (Player *candidate : inputs.own_team.teammates) {
        if (candidate->IsActive() &&
            candidate->GetID() == *control.target_player &&
            candidate != &player) {
          command.touchInfo.forcedTargetPlayer = candidate;
          break;
        }
      }
    }
    if (control.target_position) {
      command.touchInfo.useForcedTargetPosition = true;
      command.touchInfo.forcedTargetPosition = *control.target_position;
    }
    if (control.action == ControlAction::Shoot) {
      // Aim is explicit: the requested direction or target point, never an
      // automatic goal-centre selection.
      command.touchInfo.desiredDirection =
          command.touchInfo.useForcedTargetPosition
              ? (command.touchInfo.forcedTargetPosition - player.GetPosition())
                    .GetNormalized(command.desiredDirection)
              : command.desiredDirection;
      command.touchInfo.desiredPower = std::max(0.f, control.power);
    } else {
      const Vector3* forcedTargetPosition =
          command.touchInfo.useForcedTargetPosition
              ? &command.touchInfo.forcedTargetPosition
              : nullptr;
      Player* targetPlayer = nullptr;
      if (!football::sim::mechanics::GetPass(
              &player, command.desiredFunctionType,
              command.touchInfo.forcedTargetPlayer, forcedTargetPosition,
              command.touchInfo.desiredDirection,
              command.touchInfo.desiredPower, targetPlayer)) {
        // The explicit target is alive but the concrete pass could not be
        // solved. Keep locomotion, but do not report the pass as executed.
        return result(CommandBuildStatus::FallbackMovement, {movement});
      }
      command.touchInfo.targetPlayer = targetPlayer;
    }
  }
  if (control.action == ControlAction::Save) {
    // Hands legality is a Rules decision; a rejected Save still publishes the
    // movement intent.
    if (!authorization.goalkeeper_hands_allowed)
      return result(CommandBuildStatus::Rejected, {movement});
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
  }
  // Mechanical movement fallback, not a second AI decision. An unavailable
  // action must still publish a fresh locomotion intent on reset/re-entry.
  return result(CommandBuildStatus::Accepted, {command, movement});
}

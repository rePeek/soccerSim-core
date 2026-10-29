#include "player/execution/player_control_builder.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "football/ball/ball.hpp"
#include "football/pitch/pitch.hpp"
#include "player/player.hpp"
#include "football/player/player_command_validation.hpp"
#include "player/execution/player_action_mapping.hpp"
#include "player/execution/kick_targeting.hpp"

namespace {

using football::sim::player::CommandBuildStatus;

CommandBuildResult BuildResult(CommandBuildStatus status,
                               PlayerCommandQueue commands) {
  return CommandBuildResult{status, std::move(commands)};
}

// Continuous movement portion of a control input. It is always produced first:
// every rejection and fallback below publishes this command so locomotion
// continuity survives a failed discrete action.
PlayerCommand BuildMovementCommand(const PlayerControl& control,
                                   const Player& player) {
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
  return movement;
}

// Rules own restart and keeper-hands legality. Build a value-only request and
// let the Runtime-owned authorization port sample its live rule facts here,
// where the original checks ran; Player never adjudicates.
football::sim::player::PlayerActionAuthorization EvaluateAuthorization(
    const PlayerControl& control, Player& player,
    const PlayerCommandInputs& inputs) {
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
    authorization_request.defending_direction = inputs.own_team.defending_direction;
  }
  football::sim::player::PlayerActionAuthorization authorization;
  if (inputs.authorization != nullptr) {
    authorization = inputs.authorization->Evaluate(authorization_request);
  }
  return authorization;
}

struct DiscreteActionBuild {
  CommandBuildStatus status = CommandBuildStatus::MovementOnly;
  // Present only for Accepted: the discrete command that precedes the movement
  // fallback in the queue. Every other status publishes movement alone.
  std::optional<PlayerCommand> command;
};

// Discrete action portion. The caller has already handled the rule restart gate
// and the MovementOnly case; this decides whether the requested action can be
// expressed as a command at all.
DiscreteActionBuild BuildDiscreteActionCommand(
    const PlayerControl& control, Player& player,
    const PlayerCommandInputs& inputs, const PlayerCommand& movement,
    const football::sim::player::PlayerActionAuthorization& authorization) {
  if (control.action == ControlAction::Pull ||
      control.action == ControlAction::CallSupport) {
    // Defined Commands with no current execution path: Pull has no correct
    // clip, and CallSupport must be resolved by Team/Planner into another
    // player's Move Command. Never substitute an unrelated action.
    return {CommandBuildStatus::Unsupported, std::nullopt};
  }

  const bool pass_action =
      football::sim::player::IsPassControlAction(control.action);
  if (!football::sim::player::ControlActionTargetIsExplicit(control) ||
      !football::sim::player::ControlActionAimIsExplicit(control)) {
    return {CommandBuildStatus::Rejected, std::nullopt};
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
          football::sim::player::ResolveExplicitShotDirection(
              control, player.GetPosition(), command.desiredDirection);
      command.touchInfo.desiredPower = std::max(0.f, control.power);
    } else {
      const Vector3* forcedTargetPosition =
          command.touchInfo.useForcedTargetPosition
              ? &command.touchInfo.forcedTargetPosition
              : nullptr;
      Player* targetPlayer = nullptr;
      if (!football::sim::mechanics::GetPass(
              &player, command.desiredFunctionType,
              inputs.own_team.defending_direction,
              command.touchInfo.forcedTargetPlayer, forcedTargetPosition,
              command.touchInfo.desiredDirection,
              command.touchInfo.desiredPower, targetPlayer)) {
        // The explicit target is alive but the concrete pass could not be
        // solved. Keep locomotion, but do not report the pass as executed.
        return {CommandBuildStatus::FallbackMovement, std::nullopt};
      }
      command.touchInfo.targetPlayer = targetPlayer;
    }
  }
  if (control.action == ControlAction::Save) {
    // Hands legality is a Rules decision; a rejected Save still publishes the
    // movement intent.
    if (!authorization.goalkeeper_hands_allowed) {
      return {CommandBuildStatus::Rejected, std::nullopt};
    }
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
  }
  return {CommandBuildStatus::Accepted, std::move(command)};
}

}  // namespace

CommandBuildResult BuildPlayerCommands(const PlayerControl& input,
                                       Player& player,
                                       const PlayerCommandInputs& inputs) {
  // Controls are already in the single Stadium Frame; never mutate caller data.
  PlayerControl control = input;
  PlayerCommand movement = BuildMovementCommand(control, player);
  const football::sim::player::PlayerActionAuthorization authorization =
      EvaluateAuthorization(control, player, inputs);
  if (authorization.restart_gate && !authorization.action_allowed) {
    if (!authorization.movement_allowed) movement.desiredVelocityFloat = 0.f;
    return BuildResult(CommandBuildStatus::Rejected, {movement});
  }
  if (control.action == ControlAction::None) {
    return BuildResult(CommandBuildStatus::MovementOnly, {movement});
  }

  DiscreteActionBuild action = BuildDiscreteActionCommand(
      control, player, inputs, movement, authorization);
  if (action.status == CommandBuildStatus::Accepted && action.command) {
    // Mechanical movement fallback, not a second AI decision. An unavailable
    // action must still publish a fresh locomotion intent on reset/re-entry.
    return BuildResult(CommandBuildStatus::Accepted,
                       {*action.command, movement});
  }
  return BuildResult(action.status, {movement});
}

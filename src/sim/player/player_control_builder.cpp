#include "sim/player/player_control_builder.hpp"

#include <algorithm>
#include <cmath>

#include "sim/ball/ball.hpp"
#include "sim/event/touch_query.hpp"
#include "sim/rules/referee.hpp"
#include "model/pitch.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/team/team.hpp"
#include "sim/player/player.hpp"
#include "sim/player/kick_targeting.hpp"

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
  const auto runtime_frame = FromHomePitchFrame(*player.GetTeam());
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
  const auto& restart = inputs.restart;
  if (restart.active && restart.restart) {
    if (restart.restart->phase == RestartPhase::Pending) return {movement};
    if (restart.taker != &player) {
      movement.desiredVelocityFloat = 0.f;
      return {movement};
    }
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
      for (Player *candidate : player.GetTeam()->GetAllPlayers()) {
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
          &player, command.touchInfo.inputDirection, command.touchInfo.autoDirectionBias);
      command.touchInfo.desiredPower = control.power;
    } else {
      football::sim::mechanics::GetPass(&player, command.desiredFunctionType,
          command.touchInfo.inputDirection, command.touchInfo.inputPower,
          command.touchInfo.autoDirectionBias, command.touchInfo.autoPowerBias,
          command.touchInfo.desiredDirection, command.touchInfo.desiredPower,
          command.touchInfo.targetPlayer, command.touchInfo.forcedTargetPlayer);
    }
  }
  if (control.action == ControlAction::Save) {
    // Hands legality belongs to execution/rules, not a cooperative policy.
    Team *team = player.GetTeam();
    const Vector3 ball = inputs.ball.Predict(160);
    const bool backpass = inputs.touches.last_team == team->GetID() &&
        football::sim::event::LastTouchPlayer(inputs.touches, *team) != &player &&
        inputs.touches.last_team_by_type[e_TouchType_Intentional_Kicked] == team->GetID();
    if (team->GetGoalie() != &player || inputs.retainer || backpass ||
        std::fabs(ball.coords[1]) > 20.05f ||
        ball.coords[0] * -team->GetDynamicSide() > -inputs.pitch.half_length() + 16.4f)
      return {movement};
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
  }
  // Mechanical movement fallback, not a second AI decision. An unavailable
  // action must still publish a fresh locomotion intent on reset/re-entry.
  return {command, movement};
}

#include "runtime/player_authorization_port.hpp"

#include "referee/action_authorization.hpp"

namespace football::sim {

football::sim::player::PlayerActionAuthorization
PlayerAuthorizationPortImpl::Evaluate(
    const football::sim::player::PlayerAuthorizationRequest& request) const {
  football::sim::rules::PlayerActionAuthorizationInput input;
  input.restart_active = restart_.active;
  input.restart_scheduled = restart_.state.has_value();
  input.restart_phase =
      restart_.state ? restart_.state->phase : football::sim::RestartPhase::InPlay;
  input.is_restart_taker = restart_.taker == request.actor;
  if (request.save_request) {
    input.is_goalkeeper = request.is_goalkeeper;
    input.ball_retained = request.ball_retained;
    input.backpass = football::sim::rules::IsGoalkeeperBackpass(
        touches_, request.team_id, request.teammates, request.actor);
    input.predicted_ball_x = request.predicted_ball.coords[0];
    input.predicted_ball_y = request.predicted_ball.coords[1];
    input.half_length = request.half_length;
    input.dynamic_side = request.dynamic_side;
  }
  const football::sim::rules::PlayerActionAuthorization verdict =
      football::sim::rules::EvaluateActionAuthorization(input);
  football::sim::player::PlayerActionAuthorization result;
  result.restart_gate = restart_.active && restart_.state.has_value();
  result.movement_allowed = verdict.movement_allowed;
  result.action_allowed = verdict.action_allowed;
  result.goalkeeper_hands_allowed = verdict.goalkeeper_hands_allowed;
  return result;
}

}  // namespace football::sim

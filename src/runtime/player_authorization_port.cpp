#include "runtime/player_authorization_port.hpp"

#include "event/touch_query.hpp"
#include "player/player.hpp"
#include "referee/action_authorization.hpp"

namespace football::sim {

namespace {

// Owns the "a goalkeeper may not handle a deliberate kick from a teammate"
// rule's data sampling. It reads the live Event touch record and the keeper's
// roster, so it lives in Runtime rather than the value-only rule library.
bool IsGoalkeeperBackpass(const football::sim::event::TouchState& touches,
                          int team_id, std::span<Player* const> teammates,
                          const Player* actor) {
  return touches.last_team == team_id &&
      football::sim::event::LastTouchPlayer(touches, team_id, teammates) !=
          actor &&
      touches.last_team_by_type[e_TouchType_Intentional_Kicked] == team_id;
}

}  // namespace

football::sim::player::PlayerActionAuthorization
PlayerAuthorizationPortImpl::Evaluate(
    const football::sim::player::PlayerAuthorizationRequest& request) const {
  football::sim::rules::PlayerActionAuthorizationInput input;
  input.restart_active = restart_.active;
  input.restart_scheduled = restart_.state.has_value();
  input.restart_phase =
      restart_.state ? restart_.state->phase : football::sim::RestartPhase::InPlay;
  input.is_restart_taker = restart_.taker == request.actor->GetID();
  if (request.save_request) {
    input.is_goalkeeper = request.is_goalkeeper;
    input.ball_retained = request.ball_retained;
    input.backpass = IsGoalkeeperBackpass(
        touches_, request.team_id, request.teammates, request.actor);
    input.predicted_ball_x = request.predicted_ball.coords[0];
    input.predicted_ball_y = request.predicted_ball.coords[1];
    input.half_length = request.half_length;
    input.defending_direction = request.defending_direction;
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

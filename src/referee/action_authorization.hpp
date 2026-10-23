#ifndef FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP
#define FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP

#include "referee/restart_phase.hpp"

namespace football::sim::rules {

// Rule facts an actor's control request must respect. Values only: the caller
// samples them at the point where the original rule checks ran, so the decision
// reads the live match state and never a tick-start snapshot.
struct PlayerActionAuthorizationInput {
  // Restart gate. `restart_scheduled` distinguishes "no restart in flight" from
  // a restart whose state has not been created yet.
  bool restart_active = false;
  bool restart_scheduled = false;
  RestartPhase restart_phase = RestartPhase::InPlay;
  bool is_restart_taker = true;

  // Goalkeeper hands gate. Only meaningful for a Save request.
  bool is_goalkeeper = false;
  bool ball_retained = false;
  bool backpass = false;
  float predicted_ball_x = 0.0f;
  float predicted_ball_y = 0.0f;
  float half_length = 0.0f;
  int defending_direction = -1;
};

// The permissions stay distinct on purpose: a pending restart keeps movement
// but forbids the discrete action, a non-taker must also stand still, and
// keeper hands are a separate capability.
struct PlayerActionAuthorization {
  bool movement_allowed = true;
  bool action_allowed = true;
  bool goalkeeper_hands_allowed = false;
};

PlayerActionAuthorization EvaluateActionAuthorization(
    const PlayerActionAuthorizationInput& input);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP

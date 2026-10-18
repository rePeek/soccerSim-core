#ifndef FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP
#define FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP

#include <span>

#include "event/touch_state.hpp"
#include "runtime/restart_context.hpp"

class Player;

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
  int dynamic_side = -1;
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

// Owns the "a goalkeeper may not handle a deliberate kick from a teammate"
// rule. The caller supplies the live touch record and the keeper's roster.
bool IsGoalkeeperBackpass(const football::sim::event::TouchState& touches,
                          int team_id, std::span<Player* const> teammates,
                          const Player* actor);

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_ACTION_AUTHORIZATION_HPP

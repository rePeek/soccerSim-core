#include "sim/referee/action_authorization.hpp"

#include <cmath>

#include "sim/event/touch_query.hpp"
#include "sim/player/player.hpp"

namespace football::sim::rules {

bool IsGoalkeeperBackpass(const football::sim::event::TouchState& touches,
                          int team_id, std::span<Player* const> teammates,
                          const Player* actor) {
  return touches.last_team == team_id &&
      football::sim::event::LastTouchPlayer(touches, team_id, teammates) != actor &&
      touches.last_team_by_type[e_TouchType_Intentional_Kicked] == team_id;
}

PlayerActionAuthorization EvaluateActionAuthorization(
    const PlayerActionAuthorizationInput& input) {
  PlayerActionAuthorization authorization;
  if (input.restart_active && input.restart_scheduled) {
    if (input.restart_phase == RestartPhase::Pending) {
      // The restart is still being set up: no discrete action yet, but the
      // actor may keep moving to take its position.
      authorization.action_allowed = false;
      return authorization;
    }
    if (!input.is_restart_taker) {
      // Only the taker may act, and only the taker may keep moving; everyone
      // else holds position until the restart is taken.
      authorization.action_allowed = false;
      authorization.movement_allowed = false;
      return authorization;
    }
  }
  // Hands legality is a keeper-only capability: only inside the box, ball not
  // already retained and not a deliberate teammate kick. The legacy literal
  // half-width and the pitch-derived box depth are preserved.
  authorization.goalkeeper_hands_allowed =
      input.is_goalkeeper && !input.ball_retained && !input.backpass &&
      std::fabs(input.predicted_ball_y) <= 20.05f &&
      input.predicted_ball_x * -input.dynamic_side <=
          -input.half_length + 16.4f;
  return authorization;
}

}  // namespace football::sim::rules

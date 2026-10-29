#include "runtime/ball_player_contact.hpp"

#include <cmath>

#include "football/ball/ball.hpp"
#include "player/execution/player_touch_event.hpp"
#include "football/contact/player_touch_type.hpp"
#include "player/player.hpp"
#include "football/contact/player_body_collider.hpp"
#include "football/contact/passive_body_touch.hpp"
#include "football/foundation/time/tick_boundary.hpp"
#include "player/execution/player_touch_query.hpp"

using football::ball::Ball;

namespace football::sim {

BallPlayerContactResult ResolveBallPlayerContacts(
    const Ball& ball, std::span<Player* const> players,
    const BallPlayerContactInputs& inputs) {
  constexpr TickSpan kBodyBallCollisionCooldown{15};
  if (inputs.now <= inputs.last_body_collision + kBodyBallCollisionCooldown) return {};

  football::contact::PassiveBodyTouchAccumulator accumulated;
  int last_touch_team = inputs.last_touch_team;

  for (int i = 0; i < (signed int)players.size(); i++) {
    const PlayerActionState &action = players[i]->GetSimulationActionState();
    int teamID = players[i]->GetTeamID();

    int touchTimeThreshold_ms = 200;
    float oppLastTouchBias = football::sim::player::TeamTouchBias(inputs.touches,
        inputs.teams[abs(teamID - 1)].team_id,
        inputs.teams[abs(teamID - 1)].teammates, touchTimeThreshold_ms, inputs.now);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms, inputs.now);

    if (lastTouchBias <= 0.01f && oppLastTouchBias > 0.01f) {
      // Unexpected opponent touches can cause body contacts; a player's own
      // recent touch suppresses perpetually repeated collisions.
      bool collisionAnim = false;
      if (action.type == e_FunctionType_Movement || action.type == e_FunctionType_Trip || action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (action.type == e_FunctionType_Interfere || action.type == e_FunctionType_Deflect) onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        const auto sample_age = std::chrono::milliseconds{players[i]->GetReactionTime_ms() + static_cast<int>(ToMilliseconds(action.elapsed))};
        float unexpectedDistance = (inputs.observation.BallPrediction(sample_age, Seconds(1), inputs.now, ball) - ball.Predict(football::sim::TickSpan{100}).position).GetLength();
        if (unexpectedDistance > 0.5f) directionChangedUnexpectedly = true;
      }

      if (collisionAnim &&
          !inputs.teams[players[i]->GetTeamID()]
               .BallRelationOf(players[i])
               .unique_in_match &&
          (onlyWhenDirectionChangedUnexpectedly == directionChangedUnexpectedly)) {
        const float ballRadius = football::contact::PassiveBodyTouchRadius(
            action.type, inputs.teams[players[i]->GetTeamID()]
                             .BallRelationOf(players[i])
                             .locally_controllable);

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) -
             ball.state().position).GetLength() < 2.5f) {
          const PlayerBodyCollider body = BuildBodyCollider(players[i]->GetKinematicState());
          for (const BodyVolume *volume : body.GetVolumes()) {
            if (volume->IntersectsSphere(ball.state().position, ballRadius)) {
              // Read current touch records, not biases frozen before the sweep.
              const int latest_team = last_touch_team != -1
                  ? last_touch_team : inputs.fallback_last_touch_team;
              if (players[i]->GetID() ==
                      inputs.teams[players[i]->GetTeamID()].DesignatedPlayerId() &&
                  football::sim::player::TeamTouchBias(inputs.touches,
                      inputs.teams[latest_team].team_id,
                      inputs.teams[latest_team].teammates, 200, inputs.now) < 0.01f) {
                players[i]->TriggerControlledBallCollision();
              } else {
                football::contact::AccumulatePassiveBodyMotion(
                    accumulated, ball.state().position, volume->center,
                    players[i]->GetMovement(), oppLastTouchBias);
                inputs.touch_sink->OnAcceptedTouch(
                    football::sim::player::PlayerTouchEvent{inputs.now,
                        players[i]->GetID(), players[i]->GetTeamSide(),
                        e_TouchType_Accidental, ball.state().position,
                        ball.state().velocity,
                        static_cast<int>(players[i]->GetSimulationActionState().type)});
                last_touch_team = teamID;
                // Keep per-volume accumulation and touch/rule notification order.
                football::contact::AccumulatePassiveBodyBias(
                    accumulated, ball.state().position, volume->center,
                    ballRadius, volume->radius);
              }
            }
          }
        }
      }
    }
  }

  const auto response = football::contact::FinishPassiveBodyTouches(
      accumulated, ball.state().velocity);
  return {response.velocity, response.rotation_bias};
}

}  // namespace football::sim
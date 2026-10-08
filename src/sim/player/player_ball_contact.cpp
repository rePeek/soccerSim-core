#include "sim/player/player_ball_contact.hpp"

#include <cmath>

#include "football/ball/ball.hpp"
#include "sim/event/ball_touch_sink.hpp"
#include "sim/event/touch_type.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_body_collider.hpp"
#include "sim/team/team.hpp"
#include "sim/time/tick_boundary.hpp"
#include "sim/event/touch_query.hpp"

using football::ball::Ball;

namespace football::sim {

BallPlayerContactResult ResolveBallPlayerContacts(
    const Ball& ball, std::span<Player* const> players,
    const BallPlayerContactInputs& inputs) {
  constexpr TickSpan kBodyBallCollisionCooldown{15};
  if (inputs.now <= inputs.last_body_collision + kBodyBallCollisionCooldown) return {};

  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount = 0;
  int last_touch_team = inputs.last_touch_team;

  for (int i = 0; i < (signed int)players.size(); i++) {
    const PlayerActionState &action = players[i]->GetSimulationActionState();
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;
    float oppLastTouchBias = event::TeamTouchBias(inputs.touches, *inputs.teams[abs(teamID - 1)], touchTimeThreshold_ms, inputs.now);
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
        const auto history = std::chrono::milliseconds{players[i]->GetReactionTime_ms() + static_cast<int>(ToMilliseconds(action.elapsed))};
        const auto index = observation::MentalImageSampleIndex(inputs.history.size(), history);
        float unexpectedDistance = (inputs.history[index].GetBallPrediction(Seconds(1), inputs.now, ball) - ball.Predict(1000)).GetLength();
        if (unexpectedDistance > 0.5f) directionChangedUnexpectedly = true;
      }

      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly == directionChangedUnexpectedly)) {
        float boundingBoxSizeOffset = -0.1f;
        if (!players[i]->HasPossession()) boundingBoxSizeOffset += 0.03f;
        else boundingBoxSizeOffset -= 0.03f;

        if (action.type == e_FunctionType_Sliding || action.type == e_FunctionType_Interfere) {
          boundingBoxSizeOffset += 0.1f;
        }
        if (action.type == e_FunctionType_Deflect) {
          boundingBoxSizeOffset += 0.2f;
        }

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) -
             ball.Predict(0)).GetLength() < 2.5f) {
          const PlayerBodyCollider body = BuildBodyCollider(players[i]->GetKinematicState());
          for (const BodyVolume *volume : body.GetVolumes()) {
            float ballRadius = 0.11f + boundingBoxSizeOffset;
            if (volume->IntersectsSphere(ball.Predict(0), ballRadius)) {
              // Read current touch records, not biases frozen before the sweep.
              const int latest_team = last_touch_team != -1
                  ? last_touch_team : inputs.fallback_last_touch_team;
              if (players[i] == players[i]->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
                  event::TeamTouchBias(inputs.touches, *inputs.teams[latest_team], 200, inputs.now) < 0.01f) {
                players[i]->TriggerControlledBallCollision();
              } else {
                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec +=
                    (ball.Predict(0) - volume->center).GetNormalized(Vector3(0)) * movementBias +
                    players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                inputs.touch_sink->OnBallTouched(
                    {inputs.now, players[i], players[i]->GetTeam(), e_TouchType_Accidental});
                last_touch_team = teamID;
                // Keep per-volume accumulation and touch/rule notification order.
                bias += (1.0f -
                         clamp(((ball.Predict(0) - volume->center).GetLength() - ballRadius) /
                                   volume->radius, 0.0f, 1.0f)) * 0.9f + 0.1f;
              }
            }
          }
        }
      }
    }
  }

  if (!(bias > 0.0f)) return {};
  bounceVec /= (bounceCount * 1.0f);
  bounceVec.coords[2] *= 0.6f;
  bounceVec.Normalize();
  Vector3 currentMovement = ball.GetMovement();
  Vector3 fullCollisionVec = (bounceVec * 6.0f) + (bounceVec * currentMovement.GetLength() * 0.6f) + (currentMovement * -0.2f);
  bias = clamp(bias, 0.0f, 1.0f);
  bias = bias * 0.5f + 0.5f;
  Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
  if (resultVector.GetLength() > currentMovement.GetLength()) resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
  resultVector *= 0.7f;
  return {resultVector, 0.5f * bias};
}

}  // namespace football::sim
// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "sim/player/ball_approach.hpp"

#include <cmath>

#include "foundation/geometry/line.hpp"
#include "sim/query/reachability.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "sim/player/player.hpp"
#include "sim/player/humanoid/humanoid_utils.hpp"

namespace football::sim::mechanics {

unsigned int GetToBallMovement(Match *match, const MentalImage *mentalImage,
                                  Player *player,
                                  const Vector3 &desiredDirection,
                                  float desiredVelocityFloat,
                                  Vector3 &bestDirection,
                                  float &bestVelocityFloat, Vector3 &bestLookAt,
                                  float haste) {

  Vector3 playerPos = player->GetPosition();

  float movementWeight = 1.0f;
  float timeWeight = 0.0f;
  float perpendicularWeight = 0.1f;
  float previousTargetWeight = 0.0f;

  Vector3 adaptedDesiredDirection = desiredDirection;


  // precalc shortest distance to ball 'line' for perpendicularity rating (so we have a minimum value there)

  Line ballLine(mentalImage->GetBallPrediction(0).Get2D(), mentalImage->GetBallPrediction(1000).Get2D());
  float u = 0;
  float playerBallLineShortestDistance = ballLine.GetDistanceToPoint(playerPos, u);
  Vector3 playerBallShortestTargetPos = ballLine.GetVertex(0) + (ballLine.GetVertex(1) - ballLine.GetVertex(0)) * u;


  // quantize input 8- or 16-way in relation to 'ball direction space'

  Vector3 ballSpaceDirection = (mentalImage->GetBallPrediction(0).Get2D() - mentalImage->GetBallPrediction(1000).Get2D()).GetNormalized(adaptedDesiredDirection);
  radian toBallSpaceAngle = ballSpaceDirection.GetAngle2D();
  Vector3 adaptedDesiredDirectionBallSpace = adaptedDesiredDirection.GetRotated2D(-toBallSpaceAngle);

  int directions = 16;

  real angle = adaptedDesiredDirectionBallSpace.GetAngle2D();
  angle /= pi * 2.0f;
  angle = std::round(angle * directions);
  angle /= directions;
  angle *= pi * 2.0f;

  float bias = 1.0f;
  adaptedDesiredDirectionBallSpace = (adaptedDesiredDirectionBallSpace * (1.0 - bias) + (Vector3(1, 0, 0).GetRotated2D(angle) * bias)).GetNormalized(adaptedDesiredDirectionBallSpace);

  adaptedDesiredDirection = adaptedDesiredDirectionBallSpace.GetRotated2D(toBallSpaceAngle);

  //printf("ball angle: %f, result angle: %f\n", ballSpaceDirection.GetAngle2D(), adaptedDesiredDirection.GetAngle2D());


  Vector3 desiredMovement = adaptedDesiredDirection * desiredVelocityFloat;

/*
  // only use player desired movement if it's a lot different from perpendicular
  float desiredVsPerpendicularDot = adaptedDesiredDirection.GetDotProduct((playerBallShortestTargetPos - playerPos).GetNormalized(0));
  //if (desiredVsPerpendicularDot < 0.3f) movementWeight = 1.0f; else perpendicularWeight = 1.0f;
  float desiredVsPerpendicularBias = std::pow(NormalizedClamp(desiredVsPerpendicularDot, 0.0f, 1.0f), 0.5f);
  desiredVsPerpendicularBias = desiredVsPerpendicularBias * 0.5f + 0.5f;
  movementWeight = (1.0f - desiredVsPerpendicularBias);
  perpendicularWeight = desiredVsPerpendicularBias;
*/

  if (haste > 0.0f) {
    movementWeight = 0.0f;
    timeWeight = 1.0f;
    perpendicularWeight = 0.0f;
    /*
    movementWeight *= (1.0f - haste);
    timeWeight = timeWeight * (1.0f - haste) + haste;
    //effectiveTimeWeight += 1.0f; // recently added, testing
    perpendicularWeight *= (1.0f - haste);
    */
  }

  struct PossibleChoice {
    PossibleChoice() {
      rating = -10000.0f;
      time_ms = -10;
      timeNeeded_ms = -10;
    }
    float rating = 0.0f;
    int timeNeeded_ms = 0;
    int time_ms = 0;
  };

  PossibleChoice bestChoice;

  unsigned int startTime_ms = clamp(player->GetTimeNeededToGetToBall_ms(), 40, ballPredictionSize_ms - 10);
  // we can't start optimized at a later moment, because that would mean we'd aim outside the pitch to start with
  Vector3 ballPrediction = mentalImage->GetBallPrediction(startTime_ms);
  if (std::fabs(ballPrediction.coords[0]) > pitchHalfW - 0.2f ||
      std::fabs(ballPrediction.coords[1]) > pitchHalfH - 0.2f) {
    startTime_ms = 80;
  }

  int previousDesiredTargetTime_ms = player->GetDesiredTimeToBall_ms();
  int timeNeededToGetToBall_ms = player->GetTimeNeededToGetToBall_ms();

  float ffoLength = GetFrontOfFootOffsetRel(desiredVelocityFloat * 0.2f + player->GetFloatVelocity() * 0.8f, 0, 0).GetLength();

  Vector3 ballMovementRough = (mentalImage->GetBallPrediction(player->GetTimeNeededToGetToBall_ms() + 10) - mentalImage->GetBallPrediction(player->GetTimeNeededToGetToBall_ms())).Get2D() * 100.0f;
  float desiredBallDot = ballMovementRough.GetNormalized(0).GetDotProduct(adaptedDesiredDirection);
  Vector3 ballDirectionRough = ballMovementRough.GetNormalized(adaptedDesiredDirection);

  unsigned int timeStep = 1;

  for (unsigned int time_ms = startTime_ms; time_ms < ballPredictionSize_ms;
       time_ms += 10 * timeStep) {

    bool forced = false;

    Vector3 targetPos = mentalImage->GetBallPrediction(time_ms);
    if (std::fabs(targetPos.coords[0]) > pitchHalfW - 0.2f ||
        std::fabs(targetPos.coords[1]) > pitchHalfH - 0.2f) {
      forced = true;
    }
    if (!forced) if (targetPos.coords[2] >= 1.0f) continue; // unattainable
    targetPos = targetPos.Get2D();

    football::sim::query::TimeNeeded timeNeeded = football::sim::query::GetTimeNeededForDistance_ms(playerPos, player->GetMovement(), targetPos, player->GetMaxVelocity(), true, time_ms);
    unsigned int timeNeeded_ms = timeNeeded.usual_ms;

    timeStep = std::min(
        std::max(
            int(std::round((signed int)(timeNeeded_ms - time_ms) * 0.05f)) - 5,
            1),
        10);  // if ball is going to be too far away anyway, optimize by
              // skipping 'frames'
    // printf("timestep: %u, %u, %u\n", timeStep, timeNeeded_ms, time_ms);

    if (timeNeeded_ms <= time_ms) {

      // ball will ideally be slightly in front of us, simulate this.
      //targetPos -= (targetPos - playerPos).GetNormalizedMax(1.0f) * 0.25f;


      float dot = (targetPos - playerPos).GetNormalized(0).GetDotProduct(ballDirectionRough);
      /* to do this, we need to change the look at stuff below too, else we will
      walk backwards with a strange body dir sometimes (i think) if (dot >=
      0.6f) forced = true;
      //Vector3 ballMovement = mentalImage->GetBallPrediction(time_ms + 10) -
      mentalImage->GetBallPrediction(time_ms); if (ballMovementRough.GetLength()
      < sprintVelocity * 2.0f) { if (dot >= 0.4f) forced = true;
      // when ball goes slower, allow less walking in ball movement direction
      }
      */
      // if (dot >= 0.0f) forced = true; // force <= perpendicular
      float justInTimeFactor = clamp(player->GetTimeNeededToGetToBall_ms() / (float)time_ms, 0.0f, 1.0f); // lower == more time to spare
      if (justInTimeFactor < 0.35f) {
        forced = true;  // takes too long relative to how long it could take
      }
      //LANCHANGE if (dot > 0.45f) forced = true; // too shallow angle, just go to ball already
      if (dot > 0.0f) {
        forced = true;  // too shallow angle, just go to ball already
      }
      //if (std::fabs(angle) <= 0.21f * pi) forced = true;


      // --- heed desired direction

      float targetDistance = (targetPos - playerPos).GetLength();
      float targetVelocity = clamp(targetDistance * distanceToVelocityMultiplier, idleVelocity, sprintVelocity);
      Vector3 targetMovement = (targetPos - playerPos).GetNormalized(0) * targetVelocity;
      float movementRating = 1.0f - NormalizedClamp((desiredMovement - targetMovement).GetLength(), 0.0f, sprintVelocity); // > sprintvelocity will often get us farther from where we want to go to
      float directionRating = 1.0f - NormalizedClamp(std::fabs(adaptedDesiredDirection.GetAngle2D(targetMovement.GetNormalized(adaptedDesiredDirection))), 0.0f, 0.5f * pi); // > 0.5f * pi will often only get us farther from where we want to go to
      movementRating = movementRating * 0.4f + directionRating * 0.6f; // directionrating omits velocity and therefore has another quality


      // --- less time is better?

      float timeRating = 1.0f - NormalizedClamp(time_ms, 0, 5000);


      // --- rate perpendicularity targetmovement to ballmovement vector

      float perpendicularRating = 1.0f - NormalizedClamp((playerBallShortestTargetPos - targetPos).GetLength(), 0, 2.0f * sprintVelocity);


      // --- rate similarity to player's previous preferred target

      float previousTargetRating = 0.0f;
      if (previousDesiredTargetTime_ms >= timeNeededToGetToBall_ms) {
        previousTargetRating = 1.0f - std::abs(previousDesiredTargetTime_ms - timeNeededToGetToBall_ms) / 500.0f;
      }

      float rating = movementRating * movementWeight +
                     timeRating * timeWeight +
                     perpendicularRating * perpendicularWeight +
                     previousTargetRating * previousTargetWeight;

      if (rating > bestChoice.rating || (forced && bestChoice.time_ms == -10)) {
        bestChoice.rating = rating;
        bestChoice.time_ms = time_ms;
        bestChoice.timeNeeded_ms = timeNeeded_ms;
      }

    }  // timeNeeded_ms <= time_ms

    if (forced) {
      if (bestChoice.time_ms == -10) {
          // this happens if timeNeeded_ms > time_ms
        bestChoice.time_ms = time_ms;
        bestChoice.timeNeeded_ms = time_ms; // just fake it - get as close as possible
      }
      break;
    }
  }

  if (bestChoice.time_ms == -10) {
    bestChoice.time_ms = ballPredictionSize_ms - 10;
    bestChoice.timeNeeded_ms = ballPredictionSize_ms - 10;
  }

  Vector3 targetPos = mentalImage->GetBallPrediction(bestChoice.time_ms).Get2D();
  float distance = (targetPos - playerPos).GetLength();
  bestDirection = (targetPos - playerPos).GetNormalized(player->GetDirectionVec());


  // velocity

  float bestVelocityRelaxed = clamp(distance * ((bestChoice.timeNeeded_ms + defaultTouchOffset_ms) / (float)bestChoice.time_ms) * distanceToVelocityMultiplier, idleVelocity, sprintVelocity);
  float bestVelocityTimeBased = clamp((bestChoice.timeNeeded_ms / (float)bestChoice.time_ms) * player->GetMaxVelocity(), idleVelocity, sprintVelocity);
  float bestVelocityASAP = clamp(distance * distanceToVelocityMultiplier, idleVelocity, sprintVelocity);


  // simple version
  bestVelocityFloat = bestVelocityTimeBased;// bestVelocityRelaxed;
  // maybe just for sprinting? if (bestChoice.timeNeeded_ms > 500 && desiredVelocityFloat > bestVelocityFloat) bestVelocityFloat = bestVelocityFloat * 0.5f + desiredVelocityFloat * 0.5f;//sprintVelocity;
  //if (distance > 0.25f * sprintVelocity) bestVelocityFloat = clamp(desiredVelocityFloat, bestVelocityFloat, sprintVelocity); // allow premature arrival at target
  //if (bestVelocityFloat < desiredVelocityFloat && bestVelocityASAP <= desiredVelocityFloat) bestVelocityFloat = bestVelocityASAP;
  // stick to current velo, to prevent switching back and forth
  if (bestChoice.time_ms > 250) bestVelocityFloat = bestVelocityFloat * 0.97f + player->GetFloatVelocity() * 0.03f;


  // lookat dir

  Vector3 lookAtBall = (mentalImage->GetBallPrediction(20).Get2D() - playerPos).GetNormalized(adaptedDesiredDirection);
  Vector3 lookAtDirection = bestDirection;
  Vector3 lookAtDesiredDir;
  // if we're close to target, allow looking in desired direction more. if we are farther away, we don't want to walk backwards yet, it makes no sense.
  if (bestChoice.time_ms < 100) {
    // these were desiredDirection first, instead of adapted
    lookAtDesiredDir = adaptedDesiredDirection;
  } else {
    if (FloatToEnumVelocity(bestVelocityFloat) != e_Velocity_Idle) {
      lookAtDesiredDir = adaptedDesiredDirection.GetClamped2D(lookAtBall, lookAtDirection);
    } else {
      lookAtDesiredDir = adaptedDesiredDirection;
    }
  }

  // combine!
  bestLookAt = playerPos + lookAtBall.GetRotated2D(clamp(lookAtDesiredDir.GetAngle2D(lookAtBall) * 1.0f, -0.25f * pi, 0.25f * pi)) * 10.0f;
  return bestChoice.time_ms;
}

unsigned int GetBallControlMovement(
    const MentalImage *mentalImage, Player *player,
    const Vector3 &desiredDirection, float desiredVelocityFloat,
    Vector3 &bestDirection, float &bestVelocityFloat, Vector3 &bestLookAt) {

  unsigned int desiredTimeToBall_ms = 250 + defaultTouchOffset_ms;

  Vector3 toBallMovement = mentalImage->GetBallPrediction(desiredTimeToBall_ms).Get2D() - player->GetPosition();
  float toBallDistance = toBallMovement.GetLength();

  // this should get rid of short distance artifacts
  float manualDirectionStartDistanceThreshold = 0.2f;
  float manualDirectionEndDistanceThreshold = 0.4f;
  float autoDirectionBias = 1.0f;
  if (toBallDistance < manualDirectionEndDistanceThreshold) {
    autoDirectionBias = std::pow(
        NormalizedClamp(toBallDistance, manualDirectionStartDistanceThreshold,
                        manualDirectionEndDistanceThreshold),
        0.5f);
  }

  Vector3 autoDirection = toBallMovement.GetNormalized(player->GetDirectionVec());
  Vector3 manualDirection = player->GetDirectionVec();//desiredDirection;
  // test this: if (player->GetDirectionVec().GetDotProduct(desiredDirection) < 0) manualDirection = desiredDirection;

  bestDirection = autoDirection * autoDirectionBias + manualDirection * (1.0f - autoDirectionBias);
  bestDirection.Normalize(player->GetDirectionVec());

  // look direction
  Vector3 bestLookDirection = bestDirection;

  float toBallVelocity = toBallDistance * distanceToVelocityMultiplier;
  bestVelocityFloat = toBallVelocity;

  //bestVelocityFloat = bestVelocityFloat * 0.5f + RangeVelocity(bestVelocityFloat) * 0.5f; // quantization is the root of all happiness
  if (bestVelocityFloat < dribbleVelocity) {
      // don't quantize low velos
    // bestVelocityFloat = idleVelocity;
  } else {
    float clampedDesiredVelocityFloat = clamp(desiredVelocityFloat, bestVelocityFloat, bestVelocityFloat + 8.0f);
    bestVelocityFloat = clampedDesiredVelocityFloat;
    if (RangeVelocity(bestVelocityFloat) < bestVelocityFloat) bestVelocityFloat = bestVelocityFloat * 0.9f + RangeVelocity(bestVelocityFloat) * 0.1f;
    if (RangeVelocity(bestVelocityFloat) > bestVelocityFloat) bestVelocityFloat = bestVelocityFloat * 0.1f + RangeVelocity(bestVelocityFloat) * 0.9f;
  }

  bestLookAt = player->GetPosition() + bestLookDirection * 10.0f;

  return player->GetTimeNeededToGetToBall_ms();
}

}  // namespace football::sim::mechanics

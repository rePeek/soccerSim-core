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


#include "player/reachability.hpp"
#include "football/foundation/time/tick_boundary.hpp"
#include "football/ball/ball_timing.hpp"

#include <algorithm>
#include <cmath>

#include "football/ball/ball.hpp"
#include "football/foundation/math/scalar.hpp"
#include "player/player_locomotion.hpp"

namespace football::sim::player {
using football::math::clamp;

TimeNeeded GetTimeNeededForDistance_ms(const Vector3 &playerPos,
                                          const Vector3 &playerMovement,
                                          const Vector3 &targetPos,
                                          float maxVelocity, bool precise,
                                          std::optional<TickSpan> horizon) {

  TimeNeeded result;

  float optimizeDist = 16.0f;
  if (precise) optimizeDist = 48.0f;

  float initialDist = (playerPos - targetPos).GetLength();
  unsigned int defaultOptimizedTime_ms = int(
      std::round((targetPos - (playerPos + playerMovement * 0.2f)).GetLength() /
                 (maxVelocity * 0.75f) * 1000));
  if (initialDist > optimizeDist) {
    result.usual_ms = defaultOptimizedTime_ms;
    result.optimistic_ms = result.usual_ms - 200;
    return result;
  }

  // definitive best version of all versions
  // The One Version
  // or else..

  Vector3 currentPos = playerPos;
  Vector3 currentMovement = playerMovement;

  float ffo = 0.1f; // in front of foot offset (ideal ball position)
  if (currentMovement.GetLength() > playerIdleDribbleSwitch) {

    currentPos += currentMovement.GetNormalized() * ffo;
    currentPos += currentMovement * 0.01f;
  } else {
    currentPos += (targetPos - playerPos).GetNormalized(0) * ffo;
  }

  TickSpan elapsed{};
  unsigned int currentTime_ms = 0; // Local formula projection, never a search clock.
  constexpr unsigned int timeStep_ms = ToMilliseconds(TickSpan{1});
  constexpr unsigned int changeTime_ms = ToMilliseconds(TickSpan{70});
  float radius_usual = 0.28f; // starting distance from our base position where we can reach balls (effectively: leg extension length)
  float radius_optimistic = 0.9f;
  float resultingRadius_usual = radius_usual; // initial value > 0 because we don't want division by zero
  bool foundOptimisticTime = false;

  float adaptedMaxVelocity = maxVelocity * 0.94f; // don't use full maxvelocity, since the last part of that velo is very hard to attain (due to exponential air resistance)

  while (true) {
    currentTime_ms = static_cast<unsigned int>(ToMilliseconds(elapsed));
    float bias = clamp((float)currentTime_ms / (float)changeTime_ms, 0.0f, 1.0f);

    if (bias >= 1.0f) {

      // we can now simply calculate the time needed
      float remainingDistance_usual = clamp((targetPos - currentPos).GetLength() - radius_usual, 0.0f, 100000.0f);
      result.usual_ms = currentTime_ms + (remainingDistance_usual / adaptedMaxVelocity) * 1000;
      resultingRadius_usual = radius_usual + remainingDistance_usual;

      if (!foundOptimisticTime) {
        float remainingDistance_optimistic = clamp((targetPos - currentPos).GetLength() - radius_optimistic, 0.0f, 100000.0f);
        result.optimistic_ms = currentTime_ms + (remainingDistance_optimistic / adaptedMaxVelocity) * 1000;
      }

      break;

    } else {
      // manual copy: this function is called a lot, so if we use the Vector3::operator*, it's creating a lot of temp vars.
      // currentMovement = playerMovement * (1.0f - bias);
      currentMovement.coords[0] = playerMovement.coords[0] * (1.0f - bias);
      currentMovement.coords[1] = playerMovement.coords[1] * (1.0f - bias);

      //currentPos += currentMovement * timeStep_ms * 0.001f;
      currentPos.coords[0] += currentMovement.coords[0] * timeStep_ms * 0.001f;
      currentPos.coords[1] += currentMovement.coords[1] * timeStep_ms * 0.001f;

      // within this radius, we can get to a ball
      radius_usual += adaptedMaxVelocity * bias * timeStep_ms * 0.001f;
      radius_optimistic += adaptedMaxVelocity * bias * timeStep_ms * 0.001f;

      float targetDistance = (targetPos - currentPos).GetSquaredLength();
      //if (currentTime_ms > 1000 && currentTime_ms % 100 == 0) printf("currentTime_ms: %i, targetDistance: %f, currentMovementLength: %f, bias: %f, radius: %f, changeTime_ms: %i\n", currentTime_ms, targetDistance, currentMovement.GetLength(), bias, radius, changeTime_ms);
      if ((targetDistance < radius_optimistic * radius_optimistic ||
           (horizon && elapsed > *horizon)) &&
          !foundOptimisticTime) {
        result.optimistic_ms = currentTime_ms;
        foundOptimisticTime = true;
      }
      if (targetDistance < radius_usual * radius_usual ||
          (horizon && elapsed > *horizon)) {
        //currentTime_ms += int(std::round(((targetPos - currentPos).GetLength() / radius) * 40.0));
        resultingRadius_usual = radius_usual;
        result.usual_ms = currentTime_ms;
        break;
      }
    }

    elapsed += TickSpan{1};
  }

  if (horizon && elapsed > *horizon) {
    result.usual_ms = std::max(defaultOptimizedTime_ms, (currentTime_ms + 100) * 2);
    if (!foundOptimisticTime) result.optimistic_ms = result.usual_ms;
    return result;
  }

  // very, very close! just take distance as time, so we can still compare to other players properly
  if (result.usual_ms == 0) {
    result.usual_ms = int(std::round(
        clamp((targetPos - playerPos).GetLength() / resultingRadius_usual, 0.0f,
              1.0f) *
        10));
    result.optimistic_ms = result.usual_ms;
  }

  return result;

  /* too simple version
  return int(std::round((targetPos - (playerPos + playerMovement * 0.02)).GetLength() / (sprintVelocity * 0.9) * 1000));
  */
}


namespace {

// P19.4: player-local ball-control estimate, relocated verbatim from the
// Player's anonymous namespace. It reads only the actor's own motion and the
// ball's predicted movement.
bool ComputeHasPossession(const PlayerKinematicState& state,
                          const football::ball::Ball& ball) {
  const Vector3 playerMovement = state.velocity;
  if ((state.position - ball.state().position).GetLength() > 5.0) return false;
  if (std::fabs(ball.state().position.coords[2]) > 0.5) return false;
  bool distanceOK = true;
  const float radius = 1.0f;
  const Vector3 center = state.position + state.velocity * 0.05f + state.GetFacing() * 0.1f;
  if ((ball.state().position.Get2D() - center).GetLength() > radius) distanceOK = false;
  bool movementOK = true;
  const Vector3 ballMovement3D =
      (ball.Predict(football::sim::TickSpan{1}).position - ball.state().position) * 100.0f;
  if ((ballMovement3D - playerMovement).GetLength() > 6.0f) movementOK = false;
  return distanceOK && movementOK;
}

constexpr football::sim::TickSpan kReachabilityExactHorizon{50};

}  // namespace

PlayerReachabilityFacts ComputePlayerReachability(
    const PlayerReachabilityInput& input) {
  PlayerReachabilityFacts facts;
  facts.usual_ms = input.previous_usual_ms;
  facts.optimistic_ms = input.previous_optimistic_ms;
  const football::ball::Ball& ball = *input.ball;

  if (input.pure_locomotion) {
    if (input.recompute) {
      PlayerLocomotionParameters locomotion_parameters;
      locomotion_parameters.maxSpeed = input.max_velocity;
      const PlayerLocomotionReach reach =
          PlayerLocomotion::EstimateEarliestInterceptHybrid(
              input.kinematic,
              [&ball](football::sim::TickSpan horizon) { return ball.Predict(horizon).position; },
              locomotion_parameters, input.max_velocity,
              football::sim::ball_timing::kPredictionHorizon,
              kReachabilityExactHorizon,
              kLocomotionUsualReachRadius, kLocomotionOptimisticReachRadius,
              /*steady_state=*/true);
      facts.usual_ms = static_cast<unsigned int>(football::sim::ToMilliseconds(
          reach.usual.value_or(football::sim::ball_timing::kPredictionHorizon)));
      facts.optimistic_ms = static_cast<unsigned int>(football::sim::ToMilliseconds(
          reach.optimistic.value_or(football::sim::ball_timing::kPredictionHorizon)));
    }
  } else {
    // Non-locomotion actions keep the legacy heuristic because their execution
    // still follows animation root motion.
    facts.usual_ms = std::max(
        static_cast<unsigned int>(football::sim::ToMilliseconds(football::sim::ball_timing::kPredictionHorizon)),
        (unsigned int)(std::round(
            (ball.Predict(football::sim::ball_timing::kPredictionHorizon - football::sim::TickSpan{1}).position.Get2D() -
             (input.kinematic.position + input.kinematic.velocity * 0.2f)).GetLength() /
            (input.max_velocity * 0.75f) * 1000)));
    facts.optimistic_ms = facts.usual_ms;
    using football::sim::TickSpan;
    using football::sim::ToMilliseconds;
    TickSpan start{};
    if ((input.action_type == e_FunctionType_ShortPass ||
         input.action_type == e_FunctionType_LongPass ||
         input.action_type == e_FunctionType_HighPass ||
         input.action_type == e_FunctionType_Shot) && !input.touch_pending) {
      start = TickSpan{50};
    }
    bool refine = false;
    TickSpan step{1};
    TickSpan previous{};
    const bool precise = input.precise;
    for (auto candidate = start; candidate < football::sim::ball_timing::kPredictionHorizon; candidate += step) {
      const auto ms = static_cast<unsigned int>(ToMilliseconds(candidate));
      if (ball.Predict(candidate).position.coords[2] < 1.5f) {
        const auto result = football::sim::player::GetTimeNeededForDistance_ms(
            input.kinematic.position, input.kinematic.velocity, ball.Predict(candidate).position.Get2D(),
            input.max_velocity, precise, candidate);
        if (result.optimistic_ms <= ms && ms < facts.optimistic_ms) {
          facts.optimistic_ms = ms;
        }
        if (result.usual_ms <= ms) {
          if (!refine) {
            candidate = previous;
            step = TickSpan{1};
            refine = true;
          } else {
            facts.usual_ms = ms;
            break;
          }
        }
      }
      if (!refine) {
        const float balldist = (input.kinematic.position - ball.Predict(candidate).position.Get2D()).GetLength() + 0.2f;
        const float maxBallVelo = 50;
        const unsigned int timeToGo_ms = int(std::round((balldist / maxBallVelo) * 1000.0f));
        // Preserve the historical adaptive grid search (round first, then floor).
        step = TickSpan{static_cast<unsigned int>(clamp(timeToGo_ms, 10, 500)) / 10};
      } else step = TickSpan{1};
      previous = candidate;
    }
  }

  if (input.touch_anim && input.touch_pending) {
    const unsigned int animTimeToBall_ms = (input.touch_frame - input.current_frame) * 10;
    facts.usual_ms = std::min(facts.usual_ms, animTimeToBall_ms);
    facts.optimistic_ms = facts.usual_ms;
  }
  if (facts.usual_ms < defaultTouchOffset_ms) {
    facts.usual_ms = NormalizedClamp(
        ((input.kinematic.position + input.kinematic.velocity * (defaultTouchOffset_ms * 0.001)) -
            ball.Predict(football::sim::TickSpan{defaultTouchOffset_ms / football::sim::kMillisecondsPerTick}).position.Get2D()).GetLength(),
        0.0f, 0.6f) * defaultTouchOffset_ms;
    facts.optimistic_ms = facts.usual_ms;
  }
  if ((input.action_type == e_FunctionType_ShortPass ||
       input.action_type == e_FunctionType_LongPass ||
       input.action_type == e_FunctionType_HighPass ||
       input.action_type == e_FunctionType_Shot) && !input.touch_pending) {
    facts.locally_controllable = false;
  } else {
    facts.locally_controllable = ComputeHasPossession(input.kinematic, ball);
  }
  return facts;
}


}  // namespace football::sim::player

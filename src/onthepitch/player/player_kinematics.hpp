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

#ifndef _HPP_PLAYER_KINEMATICS
#define _HPP_PLAYER_KINEMATICS

#include "../../defines.hpp"
#include "core/state/player_state.hpp"

// PlayerState in core/state/player_state.hpp is the authoritative movement
// state. This alias keeps the legacy type name working during migration.
using PlayerKinematicState = PlayerState;

// Inputs are intentionally independent from PlayerCommand so that action
// execution can later impose movement constraints without exposing Humanoid.
struct PlayerKinematicInput {
  blunted::Vector3 desiredVelocity = blunted::Vector3(0);
  blunted::Vector3 desiredFacing = blunted::Vector3(0, -1, 0);
};

struct PlayerKinematicParameters {
  float maxSpeed = 8.0f;
  float acceleration = 12.0f;
  float braking = 16.0f;
  float maxTurnRate = 4.0f;  // radians per second
};

class PlayerKinematics {
 public:
  static void Step(PlayerKinematicState &state,
                   const PlayerKinematicInput &input,
                   const PlayerKinematicParameters &parameters, float dt) {
    DO_VALIDATION;
    assert(dt > 0.0f);
    assert(parameters.maxSpeed >= 0.0f);
    assert(parameters.acceleration >= 0.0f);
    assert(parameters.braking >= 0.0f);
    assert(parameters.maxTurnRate >= 0.0f);

    blunted::Vector3 desiredVelocity = input.desiredVelocity.Get2D();
    const float desiredSpeed = desiredVelocity.GetLength();
    if (desiredSpeed > parameters.maxSpeed) {
      desiredVelocity =
          desiredVelocity.GetNormalized(blunted::Vector3(0)) *
          parameters.maxSpeed;
    }

    const blunted::Vector3 velocityDelta = desiredVelocity - state.velocity;
    const float changeLimit =
        (desiredVelocity.GetLength() < state.velocity.GetLength() ? parameters.braking
                                                  : parameters.acceleration) *
        dt;
    if (velocityDelta.GetLength() > changeLimit && changeLimit > 0.0f) {
      state.velocity += velocityDelta.GetNormalized(blunted::Vector3(0)) *
                        changeLimit;
    } else {
      state.velocity = desiredVelocity;
    }
    state.velocity.coords[2] = 0.0f;

    const blunted::Vector3 currentFacing =
        state.facing.Get2D().GetNormalized(blunted::Vector3(0, -1, 0));
    const blunted::Vector3 desiredFacing =
        input.desiredFacing.Get2D().GetNormalized(currentFacing);
    const float requestedTurn = desiredFacing.GetAngle2D(currentFacing);
    const float turnLimit = parameters.maxTurnRate * dt;
    const float appliedTurn =
        blunted::clamp(requestedTurn, -turnLimit, turnLimit);
    state.facing = currentFacing.GetRotated2D(appliedTurn).GetNormalized(
        currentFacing);
    state.facing.coords[2] = 0.0f;

    state.position += state.velocity * dt;
    state.position.coords[2] = 0.0f;
  }
};

#endif

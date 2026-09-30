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

#include "env/defines.hpp"

// Explicit movement state for a player. During the initial shadow phase it is
// synchronized from Humanoid; PlayerKinematics will later produce it directly
// without animation root motion.
struct PlayerKinematicState {
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(0, -1, 0);
  // Temporary exact shadow of Humanoid's animation-derived body pose. It is
  // deliberately separate from facing: the latter is locomotion direction.
  blunted::Vector3 bodyFacing = blunted::Vector3(0, -1, 0);
  float speed = 0.0f;

  // Mirrors position and velocity like the legacy spatial state. Facing and
  // bodyFacing are deliberately left untouched: HumanoidBase's spatial state
  // mirror only negates position and movements, so changing either here would
  // diverge from the mirrored legacy actor until the next Process() resync.
  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(position);
    state->process(velocity);
    state->process(facing);
    state->process(bodyFacing);
    state->process(speed);
  }
};

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
        (desiredVelocity.GetLength() < state.speed ? parameters.braking
                                                  : parameters.acceleration) *
        dt;
    if (velocityDelta.GetLength() > changeLimit && changeLimit > 0.0f) {
      state.velocity += velocityDelta.GetNormalized(blunted::Vector3(0)) *
                        changeLimit;
    } else {
      state.velocity = desiredVelocity;
    }
    state.velocity.coords[2] = 0.0f;
    state.speed = state.velocity.GetLength();

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

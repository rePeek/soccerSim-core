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

#include "football/foundation/math/vector3.hpp"


// Explicit movement state for a player. During the initial shadow phase it is
// synchronized from Humanoid; PlayerKinematics will later produce it directly
// without animation root motion.
struct PlayerKinematicState {
  football::math::Vector3 position = football::math::Vector3(0);
  football::math::Vector3 velocity = football::math::Vector3(0);
  football::math::Vector3 facing = football::math::Vector3(0, -1, 0);
  // Temporary exact shadow of Humanoid's animation-derived body pose. It is
  // deliberately separate from facing: the latter is locomotion direction.
  football::math::Vector3 bodyFacing = football::math::Vector3(0, -1, 0);
  float GetSpeed() const { return velocity.GetLength(); }

  // Derived locomotion heading: the velocity direction while numerically
  // moving, otherwise the body facing. This replaces the stored `facing`
  // field (STATE-1.2): the next start-from-standstill reference is the body
  // facing, not a separate hidden heading.
  football::math::Vector3 GetFacing() const {
    constexpr float kEpsilon = 1e-4f;
    if (GetSpeed() > kEpsilon) {
      return velocity.Get2D().GetNormalized();
    }
    return bodyFacing.Get2D().GetNormalized(football::math::Vector3(0, -1, 0));
  }

  // Mirrors position and velocity like the legacy spatial state. Facing and
  // bodyFacing are deliberately left untouched: HumanoidBase's spatial state
  // mirror only negates position and movements, so changing either here would
  // diverge from the mirrored legacy actor until the next Process() resync.
  void Mirror() {
    position.Mirror();
    velocity.Mirror();
  }

};

// Inputs are intentionally independent from PlayerCommand so that action
// execution can later impose movement constraints without exposing Humanoid.
struct PlayerKinematicInput {
  football::math::Vector3 desiredVelocity = football::math::Vector3(0);
  football::math::Vector3 desiredFacing = football::math::Vector3(0, -1, 0);
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
    assert(dt > 0.0f);
    assert(parameters.maxSpeed >= 0.0f);
    assert(parameters.acceleration >= 0.0f);
    assert(parameters.braking >= 0.0f);
    assert(parameters.maxTurnRate >= 0.0f);

    football::math::Vector3 desiredVelocity = input.desiredVelocity.Get2D();
    const float desiredSpeed = desiredVelocity.GetLength();
    if (desiredSpeed > parameters.maxSpeed) {
      desiredVelocity =
          desiredVelocity.GetNormalized(football::math::Vector3(0)) *
          parameters.maxSpeed;
    }

    const football::math::Vector3 velocityDelta = desiredVelocity - state.velocity;
    const float changeLimit =
        (desiredVelocity.GetLength() < state.GetSpeed() ? parameters.braking
                                                      : parameters.acceleration) *
        dt;
    if (velocityDelta.GetLength() > changeLimit && changeLimit > 0.0f) {
      state.velocity += velocityDelta.GetNormalized(football::math::Vector3(0)) *
                        changeLimit;
    } else {
      state.velocity = desiredVelocity;
    }
    state.velocity.coords[2] = 0.0f;

    const football::math::Vector3 currentFacing =
        state.facing.Get2D().GetNormalized(football::math::Vector3(0, -1, 0));
    const football::math::Vector3 desiredFacing =
        input.desiredFacing.Get2D().GetNormalized(currentFacing);
    const float requestedTurn = desiredFacing.GetAngle2D(currentFacing);
    const float turnLimit = parameters.maxTurnRate * dt;
    const float appliedTurn =
        football::math::clamp(requestedTurn, -turnLimit, turnLimit);
    state.facing = currentFacing.GetRotated2D(appliedTurn).GetNormalized(
        currentFacing);
    state.facing.coords[2] = 0.0f;

    state.position += state.velocity * dt;
    state.position.coords[2] = 0.0f;
  }
};

#endif

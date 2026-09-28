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

#ifndef _HPP_CORE_PHYSICS_BALL_PHYSICS
#define _HPP_CORE_PHYSICS_BALL_PHYSICS

#include <algorithm>
#include <cmath>

#include "core/state/ball_state.hpp"

// Physical constants extracted from the legacy Ball::CalculatePrediction()
// (Phase 7B). No woodwork/net collisions yet (Phase 7D), no Match dependency.
struct BallPhysicsParams {
  float bounce = 0.62f;         // 1 = full bounce, 0 = no bounce
  float linearBounce = 0.06f;   // bigger = more brake force
  float drag = 0.015f;          // bigger = more
  float friction = 0.04f;       // bigger = more
  float linearFriction = 1.6f;  // bigger = more, arbitrary scale
  float gravity = -9.81f;
  float grassHeight = 0.025f;
  float radius = 0.11f;
};

// Single-step ball integration: free motion (gravity, drag, swerve) plus
// ground interaction (bounce, ground friction, ground-induced rotation).
// Pure function of the input state. Woodwork and net collisions stay in the
// legacy CalculatePrediction until Phase 7D.
struct BallPhysics {
  static BallState Step(const BallState& current, float dt,
                        const BallPhysicsParams& p) {
    BallState next = current;
    blunted::Vector3 momentum = current.momentum;
    blunted::Vector3 pos = current.position;
    blunted::Quaternion rotation_ms = current.rotation_ms;
    blunted::Quaternion orientation = current.orientation;

    float frictionFactor = 0.0f;

    // Gravity: vz = vz0 + g * t.
    momentum.coords[2] = momentum.coords[2] + p.gravity * dt;

    // Air resistance.
    float momentumVelo = momentum.GetLength();
    float momentumVeloDragged =
        momentumVelo - p.drag * std::pow(momentumVelo, 2.0f) * dt;
    momentum = momentum.GetNormalized(0) * momentumVeloDragged;

    // Grass influence (0 == no friction, 1 == all friction).
    float ballBottom = pos.coords[2] - p.radius;
    float grassInfluenceBias =
        blunted::clamp(1.0f - (ballBottom / p.grassHeight), 0.0f, 1.0f);
    grassInfluenceBias = std::pow(grassInfluenceBias, 0.7f);

    // Bounce.
    if (pos.coords[2] < p.radius) {
      if (momentum.coords[2] < 0.0f) {
        frictionFactor = blunted::NormalizedClamp(
            -momentum.coords[2] - 0.5f, 0.0f, 12.0f);
        momentum.coords[2] = -momentum.coords[2] * p.bounce;
        momentum.coords[2] =
            std::max(momentum.coords[2] - p.linearBounce, 0.0f);
      }
      pos.coords[2] = p.radius;
    }

    // Ground friction.
    if (pos.coords[2] < p.radius + p.grassHeight) {
      float adaptedFriction = (p.friction * grassInfluenceBias);
      blunted::Vector3 xy = momentum.Get2D();
      float velo = xy.GetLength();
      float newVelo = velo - adaptedFriction * std::pow(velo, 2.0f) * dt;
      newVelo = blunted::clamp(
          newVelo - (p.linearFriction * grassInfluenceBias * dt), 0.0f,
          100000.0f);
      xy.Normalize(blunted::Vector3(0));
      xy *= newVelo;
      momentum.coords[0] = xy.coords[0];
      momentum.coords[1] = xy.coords[1];
    }

    // Ground-induced rotation + rotation-induced ground friction.
    if (pos.coords[2] < p.radius + p.grassHeight) {
      blunted::radian xR, yR;
      xR = momentum.coords[1] / p.radius;
      yR = momentum.coords[0] / p.radius;

      blunted::Quaternion rotX;
      rotX.SetAngleAxis(
          blunted::clamp(xR * 0.001f, -blunted::pi * 0.49f,
                         blunted::pi * 0.49f),
          blunted::Vector3(-1, 0, 0));
      blunted::Quaternion rotY;
      rotY.SetAngleAxis(
          blunted::clamp(yR * 0.001f, -blunted::pi * 0.49f,
                         blunted::pi * 0.49f),
          blunted::Vector3(0, 1, 0));

      blunted::Quaternion groundRot = rotX * rotY;

      blunted::Quaternion oldToNewRotation =
          rotation_ms.GetRotationTo(groundRot).GetNormalized();
      blunted::radian rotationChangePerSecond =
          std::fabs(oldToNewRotation.GetRotationAngle(
              blunted::QUATERNION_IDENTITY)) *
          1000.0f;

      blunted::radian maxRotationChangePerSecond =
          1.0f * blunted::pi * grassInfluenceBias;
      if (frictionFactor > 0.0f) {
        maxRotationChangePerSecond += 4.0f * blunted::pi;
      }
      blunted::radian factor = 1.0f;
      if (rotationChangePerSecond > maxRotationChangePerSecond) {
        factor = maxRotationChangePerSecond / rotationChangePerSecond;
      }
      if (factor < 1.0f) {
        oldToNewRotation = oldToNewRotation.GetRotationMultipliedBy(factor);
      }

      blunted::Quaternion newRotationPredict_ms =
          oldToNewRotation * rotation_ms;

      blunted::real x, y, z;
      rotation_ms.GetAngles(x, y, z);
      x = -x;

      blunted::Vector3 ballRotationMomentum;
      ballRotationMomentum.coords[0] = y * p.radius * 1000.0f;
      ballRotationMomentum.coords[1] = x * p.radius * 1000.0f;

      float rotBias = 0.01f;
      rotBias *= grassInfluenceBias;
      if (frictionFactor > 0.0f) {
        rotBias += 0.5f * frictionFactor;
      }
      rotBias = blunted::clamp(rotBias, 0.0f, 1.0f);
      momentum.coords[0] =
          momentum.coords[0] * (1.0f - rotBias) +
          ballRotationMomentum.coords[0] * rotBias;
      momentum.coords[1] =
          momentum.coords[1] * (1.0f - rotBias) +
          ballRotationMomentum.coords[1] * rotBias;

      rotation_ms = newRotationPredict_ms;
    }

    // Magnus effect (swerve).
    {
      blunted::Vector3 rotVec;
      rotation_ms.GetAngles(rotVec.coords[0], rotVec.coords[1],
                            rotVec.coords[2]);
      rotVec *= 10.0f;

      float swerveAmount =
          blunted::NormalizedClamp(momentum.GetLength(), 0.0f, 70.0f);
      swerveAmount =
          std::pow(std::sin(swerveAmount * blunted::pi * 0.94f), 2.6f);
      blunted::Vector3 adaptedMomentumPredict =
          momentum.GetNormalized(0) * swerveAmount * 30.0f;

      blunted::Vector3 swerve =
          adaptedMomentumPredict.GetCrossProduct(-rotVec) * 1.0;

      momentum += swerve * dt;
    }

    // Position integration.
    pos += momentum * dt;

    // Orientation integration.
    blunted::Vector3 rotationVector;
    rotation_ms.GetAngles(rotationVector.coords[0], rotationVector.coords[1],
                          rotationVector.coords[2]);
    rotationVector *= dt / 0.001f;
    blunted::Quaternion rotationPredictTimeStepped;
    rotationPredictTimeStepped.SetAngles(rotationVector.coords[0],
                                         rotationVector.coords[1],
                                         rotationVector.coords[2]);
    orientation = rotationPredictTimeStepped * orientation;

    next.position = pos;
    next.momentum = momentum;
    next.rotation_ms = rotation_ms;
    next.orientation = orientation;
    return next;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_PHYSICS
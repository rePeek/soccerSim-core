#ifndef _HPP_CORE_PHYSICS_BALL_PHYSICS
#define _HPP_CORE_PHYSICS_BALL_PHYSICS

#include <algorithm>
#include <cmath>

#include "core/state/ball_state.hpp"

// Physical constants extracted from the legacy Ball::CalculatePrediction()
// (Phase 7B). No Match dependency.
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

// Goal / pitch geometry (Phase 7D). Values match the legacy constants in
// gamedefines.hpp so woodwork contact stays numerically aligned.
struct GoalGeometry {
  float halfWidth = 55.0f;      // pitchHalfW
  float goalHalfWidth = 3.7f;   // y extent of the goal mouth
  float goalHeight = 2.5f;      // z extent of the goal mouth
  float postRadius = 0.07f;
  float postAbsorbInv = 0.8f;
};

// Post and crossbar contact (Phase 7E). Geometry-only, extracted verbatim
// from the legacy CalculatePrediction; modifies position and momentum.
inline void ApplyWoodwork(blunted::Vector3& pos, blunted::Vector3& momentum,
                          const BallPhysicsParams& p, const GoalGeometry& g) {
  const float ballRadius = p.radius;
  const float postRadius = g.postRadius;

  // Posts.
  if (pos.coords[2] < g.goalHeight + ballRadius + postRadius &&
      (pos.Get2D().GetAbsolute() -
       blunted::Vector3(g.halfWidth, g.goalHalfWidth, 0))
              .GetLength() < ballRadius + postRadius) {
    blunted::Vector3 normal;
    if (pos.coords[0] < 0) {
      if (pos.coords[1] < 0) {
        normal = (pos.Get2D() -
                  blunted::Vector3(-g.halfWidth, -g.goalHalfWidth, 0))
                     .GetNormalized(blunted::Vector3(1, 0, 0));
        float nextPosZ = pos.coords[2];
        pos = blunted::Vector3(-g.halfWidth, -g.goalHalfWidth, 0) +
              normal * (postRadius + ballRadius);
        pos.coords[2] = nextPosZ;
      } else {
        normal = (pos.Get2D() -
                  blunted::Vector3(-g.halfWidth, g.goalHalfWidth, 0))
                     .GetNormalized(blunted::Vector3(1, 0, 0));
        float nextPosZ = pos.coords[2];
        pos = blunted::Vector3(-g.halfWidth, g.goalHalfWidth, 0) +
              normal * (postRadius + ballRadius);
        pos.coords[2] = nextPosZ;
      }
    } else {
      if (pos.coords[1] < 0) {
        normal = (pos.Get2D() -
                  blunted::Vector3(g.halfWidth, -g.goalHalfWidth, 0))
                     .GetNormalized(blunted::Vector3(-1, 0, 0));
        float nextPosZ = pos.coords[2];
        pos = blunted::Vector3(g.halfWidth, -g.goalHalfWidth, 0) +
              normal * (postRadius + ballRadius);
        pos.coords[2] = nextPosZ;
      } else {
        normal = (pos.Get2D() -
                  blunted::Vector3(g.halfWidth, g.goalHalfWidth, 0))
                     .GetNormalized(blunted::Vector3(-1, 0, 0));
        float nextPosZ = pos.coords[2];
        pos = blunted::Vector3(g.halfWidth, g.goalHalfWidth, 0) +
              normal * (postRadius + ballRadius);
        pos.coords[2] = nextPosZ;
      }
    }
    momentum = (momentum.Get2D().GetNormalized(normal) + (normal * 1.1f))
                   .GetNormalized() *
                   momentum.Get2D().GetLength() * g.postAbsorbInv +
               (blunted::Vector3(0, 0, 1) * momentum.coords[2]);
  }

  // Crossbar.
  blunted::Vector3 nextPosXZ = pos * blunted::Vector3(1, 0, 1);
  if ((nextPosXZ.GetAbsolute() -
       blunted::Vector3(g.halfWidth, 0, g.goalHeight))
              .GetLength() < ballRadius + postRadius &&
      std::fabs(pos.coords[1]) < g.goalHalfWidth + ballRadius + postRadius) {
    blunted::Vector3 normal;
    if (pos.coords[0] < 0) {
      normal = (nextPosXZ -
                blunted::Vector3(-g.halfWidth, 0, g.goalHeight))
                   .GetNormalized(blunted::Vector3(0, 0, 1));
      float nextPosY = pos.coords[1];
      pos = blunted::Vector3(-g.halfWidth, 0, g.goalHeight) +
            normal * (postRadius + ballRadius);
      pos.coords[1] = nextPosY;
    } else {
      normal = (nextPosXZ -
                blunted::Vector3(g.halfWidth, 0, g.goalHeight))
                   .GetNormalized(blunted::Vector3(0, 0, -1));
      float nextPosY = pos.coords[1];
      pos = blunted::Vector3(g.halfWidth, 0, g.goalHeight) +
            normal * (postRadius + ballRadius);
      pos.coords[1] = nextPosY;
    }
    blunted::Vector3 momentumPredictXZ = momentum * blunted::Vector3(1, 0, 1);
    momentum = (momentumPredictXZ.GetNormalized(normal) + (normal * 1.1f))
                   .GetNormalized() *
                   momentumPredictXZ.GetLength() * g.postAbsorbInv +
               (blunted::Vector3(0, 1, 0) * momentum.coords[1]);
  }
}


// Single-step ball integration: free motion (gravity, drag, swerve), ground
// interaction (bounce, ground friction, ground-induced rotation) and optional
// goal-frame contact (post/crossbar). Pure function of the input state.
// Net physics was removed (visual only); no Match dependency.
struct BallPhysics {
  static BallState Step(const BallState& current, float dt,
                        const BallPhysicsParams& p, bool apply_woodwork,
                        const GoalGeometry& goal) {
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

    // Woodwork contact (post / crossbar).
    // Legacy prediction compatibility: the real/current step checks goal-frame
    // contact, cached future prediction steps historically do not. Keeping this
    // flag preserves regression-exact behavior; a future physics-correctness
    // pass may check contact on every step.
    if (apply_woodwork) {
      ApplyWoodwork(pos, momentum, p, goal);
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
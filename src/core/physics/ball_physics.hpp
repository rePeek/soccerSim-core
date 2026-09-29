#ifndef _HPP_CORE_PHYSICS_BALL_PHYSICS
#define _HPP_CORE_PHYSICS_BALL_PHYSICS

#include <algorithm>
#include <cmath>

#include "core/state/ball_state.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/physics/ball_dynamics.hpp"

// Legacy contact-response parameters, kept apart from the v2 free-flight
// parameters in BallDynamicsParams. The old `gravity` and `drag` knobs have
// moved to BallDynamicsParams; the remaining fields only drive ground and
// woodwork contact.
struct BallPhysicsParams {
  float bounce = 0.62f;         // 1 = full bounce, 0 = no bounce
  float linearBounce = 0.06f;   // bigger = more brake force
  float friction = 0.04f;       // bigger = more
  float linearFriction = 1.6f;  // bigger = more, arbitrary scale
  float grassHeight = 0.025f;
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
// from the legacy CalculatePrediction; modifies position and velocity.
inline void ApplyWoodwork(blunted::Vector3& pos, blunted::Vector3& velocity,
                          const football::domain::BallProfile& ball,
                          const GoalGeometry& g) {
  const float ballRadius = ball.radius;
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
    velocity = (velocity.Get2D().GetNormalized(normal) + (normal * 1.1f))
                   .GetNormalized() *
                   velocity.Get2D().GetLength() * g.postAbsorbInv +
               (blunted::Vector3(0, 0, 1) * velocity.coords[2]);
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
    blunted::Vector3 velocityPredictXZ = velocity * blunted::Vector3(1, 0, 1);
    velocity = (velocityPredictXZ.GetNormalized(normal) + (normal * 1.1f))
                   .GetNormalized() *
                   velocityPredictXZ.GetLength() * g.postAbsorbInv +
               (blunted::Vector3(0, 1, 0) * velocity.coords[1]);
  }
}

// Single-step ball integration. Free flight uses the v2 BallDynamics
// (gravity, quadratic drag, Magnus, viscous angular drag); contact response
// (ground bounce/friction, ground-induced rotation, woodwork) remains the
// legacy model for now so each behaviour change can be measured separately.
struct BallPhysics {
  static BallState Step(const BallState& current, float dt,
                        const football::domain::BallProfile& ball,
                        const BallPhysicsParams& p, bool apply_woodwork,
                        const GoalGeometry& goal) {
    DO_VALIDATION;
    BallState next = current;

    // ---- Free flight: acceleration (velocity + angular velocity). ----
    BallDynamics::ApplyForces(next, dt, ball, BallDynamicsParams());

    float frictionFactor = 0.0f;

    // Grass influence (0 == no friction, 1 == all friction).
    float ballBottom = next.position.coords[2] - ball.radius;
    float grassInfluenceBias =
        blunted::clamp(1.0f - (ballBottom / p.grassHeight), 0.0f, 1.0f);
    grassInfluenceBias = std::pow(grassInfluenceBias, 0.7f);

    // Ground bounce (legacy contact response).
    if (next.position.coords[2] < ball.radius) {
      if (next.velocity.coords[2] < 0.0f) {
        frictionFactor = blunted::NormalizedClamp(
            -next.velocity.coords[2] - 0.5f, 0.0f, 12.0f);
        next.velocity.coords[2] = -next.velocity.coords[2] * p.bounce;
        next.velocity.coords[2] =
            std::max(next.velocity.coords[2] - p.linearBounce, 0.0f);
      }
      next.position.coords[2] = ball.radius;
    }

    // Ground friction (legacy).
    if (next.position.coords[2] < ball.radius + p.grassHeight) {
      float adaptedFriction = (p.friction * grassInfluenceBias);
      blunted::Vector3 xy = next.velocity.Get2D();
      float velo = xy.GetLength();
      float newVelo = velo - adaptedFriction * std::pow(velo, 2.0f) * dt;
      newVelo = blunted::clamp(
          newVelo - (p.linearFriction * grassInfluenceBias * dt), 0.0f,
          100000.0f);
      xy.Normalize(blunted::Vector3(0));
      xy *= newVelo;
      next.velocity.coords[0] = xy.coords[0];
      next.velocity.coords[1] = xy.coords[1];
    }

    // Woodwork contact (post / crossbar), legacy geometry.
    if (apply_woodwork) {
      ApplyWoodwork(next.position, next.velocity, ball, goal);
    }

    // Ground-induced rotation + rotation-induced ground friction. Legacy
    // behaviour re-expressed with a Vector3 angular velocity in rad/s.
    if (next.position.coords[2] < ball.radius + p.grassHeight) {
      const float invRadius = 1.0f / ball.radius;
      // No-slip rolling angular velocity. The legacy quaternion path clamped
      // each axis to ~1540 rad/s (0.49 pi rad per millisecond), unreachable in
      // practice, so that clamp is omitted.
      blunted::Vector3 rollingAngularVelocity(
          -next.velocity.coords[1] * invRadius,
          next.velocity.coords[0] * invRadius, 0.0f);

      const float maxRotationRate = blunted::pi * grassInfluenceBias +
                                    (frictionFactor > 0.0f
                                         ? 4.0f * blunted::pi
                                         : 0.0f);
      const blunted::Vector3 rotationDelta =
          rollingAngularVelocity - next.angularVelocity;
      const float rotationDeltaLength = rotationDelta.GetLength();
      if (rotationDeltaLength > maxRotationRate) {
        next.angularVelocity +=
            rotationDelta.GetNormalized(blunted::Vector3(0)) * maxRotationRate;
      } else {
        next.angularVelocity = rollingAngularVelocity;
      }

      // Spin feeds back into the ground velocity (legacy rotation-induced
      // ground friction): bias xy velocity toward the rolling-equivalent value.
      blunted::Vector3 spinLinearVelocity;
      spinLinearVelocity.coords[0] = next.angularVelocity.coords[1] * ball.radius;
      spinLinearVelocity.coords[1] = -next.angularVelocity.coords[0] * ball.radius;
      spinLinearVelocity.coords[2] = 0.0f;

      float rotBias = 0.01f * grassInfluenceBias;
      if (frictionFactor > 0.0f) rotBias += 0.5f * frictionFactor;
      rotBias = blunted::clamp(rotBias, 0.0f, 1.0f);
      next.velocity.coords[0] = next.velocity.coords[0] * (1.0f - rotBias) +
                                spinLinearVelocity.coords[0] * rotBias;
      next.velocity.coords[1] = next.velocity.coords[1] * (1.0f - rotBias) +
                                spinLinearVelocity.coords[1] * rotBias;
    }

    // Position integration, after every velocity/position correction.
    next.position += next.velocity * dt;

    // Orientation integration.
    BallDynamics::IntegrateOrientation(next, dt);

    return next;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_PHYSICS
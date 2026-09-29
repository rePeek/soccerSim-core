#ifndef _HPP_CORE_PHYSICS_BALL_PHYSICS
#define _HPP_CORE_PHYSICS_BALL_PHYSICS

#include <algorithm>
#include <cmath>

#include "core/state/ball_state.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/physics/ball_dynamics.hpp"
#include "core/contact/ball_contact.hpp"
#include "core/contact/ball_contact_resolver.hpp"
#include "core/contact/ball_ground_contact.hpp"
#include "core/physics/ground_dynamics.hpp"



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

// Single-step ball integration: BallDynamics (free flight) -> ground impact
// (detector + resolver + positional correction) -> woodwork -> ground
// persistent dynamics (sliding/rolling/rest) -> integration.
struct BallPhysics {
  static BallState Step(const BallState& current, float dt,
                        const football::domain::BallProfile& ball,
                        bool apply_woodwork,
                        const GoalGeometry& goal) {
    DO_VALIDATION;
    BallState next = current;

    // ---- Free flight: acceleration (velocity + angular velocity). ----
    BallDynamics::ApplyForces(next, dt, ball, BallDynamicsParams());

    // Ground impact: detector -> impulse resolver -> positional correction.
    const auto groundContact =
        football::contact::DetectBallGroundContact(next, ball);
    if (groundContact) {
      football::contact::ContactMaterial material;
      football::contact::BallContactResolver::Resolve(next, ball, *groundContact,
                                                       material);

      // Positional correction is independent of the impulse: even a ball
      // that is already separating gets lifted back onto the surface.
      next.position += groundContact->normal * groundContact->penetration;
    }

    // Woodwork contact (post / crossbar), legacy geometry (7G-7e replaces).
    if (apply_woodwork) {
      ApplyWoodwork(next.position, next.velocity, ball, goal);
    }

    // Ground persistent dynamics (7G-7d-c): sliding -> rolling -> rest.
    const GroundDynamicsParams groundParams;
    if (next.position.coords[2] <= ball.radius + groundParams.contactTolerance) {
      GroundDynamics::Step(next, dt, ball, groundParams);
    }

    // Position integration, after every velocity/position correction.
    next.position += next.velocity * dt;

    // Orientation integration.
    BallDynamics::IntegrateOrientation(next, dt);

    return next;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_PHYSICS
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
#include "core/contact/capsule_collider.hpp"
#include "core/contact/sphere_capsule_contact.hpp"
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

// 7G-7e: woodwork contact is detector -> resolver, the same pipeline as the
// ground. Posts are vertical capsules at the four goal-mouth corners and the
// crossbar is a horizontal capsule along y at each end of the pitch.
inline std::optional<football::contact::BallContact> DetectBallWoodworkContact(
    const BallState &state, const football::domain::BallProfile &ball,
    const GoalGeometry &g) {
  const float postX[2] = {-g.halfWidth, g.halfWidth};
  const float postY[2] = {-g.goalHalfWidth, g.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      football::contact::CapsuleCollider post;
      post.tipA = blunted::Vector3(x, y, 0.0f);
      post.tipB = blunted::Vector3(x, y, g.goalHeight);
      post.radius = g.postRadius;
      if (auto contact = football::contact::DetectSphereCapsuleContact(
              state.position, ball.radius, post)) {
        return contact;
      }
    }
  }

  const float barX[2] = {-g.halfWidth, g.halfWidth};
  for (float x : barX) {
    football::contact::CapsuleCollider bar;
    bar.tipA = blunted::Vector3(x, -g.goalHalfWidth, g.goalHeight);
    bar.tipB = blunted::Vector3(x, g.goalHalfWidth, g.goalHeight);
    bar.radius = g.postRadius;
    if (auto contact = football::contact::DetectSphereCapsuleContact(
            state.position, ball.radius, bar)) {
      return contact;
    }
  }
  return std::nullopt;
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

    // Woodwork contact (7G-7e): detector -> impulse resolver -> positional
    // correction, the same pipeline as the ground.
    if (apply_woodwork) {
      if (const auto woodworkContact =
              DetectBallWoodworkContact(next, ball, goal)) {
        football::contact::ContactMaterial material;
        football::contact::BallContactResolver::Resolve(next, ball, *woodworkContact,
                                                         material);
        next.position += woodworkContact->normal * woodworkContact->penetration;
      }
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
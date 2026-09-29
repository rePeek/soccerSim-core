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
#include "core/contact/sweep_sphere_capsule.hpp"
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

// 7G-8-b: swept woodwork contact. Same capsules as the discrete detector,
// but the sphere centre moves along the chord start -> end; returns the
// earliest timeWithinTick across posts and crossbar.
inline std::optional<football::contact::BallContact> SweepBallWoodworkContact(
    const blunted::Vector3 &start, const blunted::Vector3 &end,
    const football::domain::BallProfile &ball, const GoalGeometry &g,
    float dt) {
  std::optional<football::contact::BallContact> earliest;
  const auto consider = [&](const football::contact::CapsuleCollider &capsule) {
    if (auto c = football::contact::SweepSphereCapsule(
            start, end, ball.radius, capsule, dt)) {
      if (!earliest || c->timeWithinTick < earliest->timeWithinTick) {
        earliest = c;
      }
    }
  };

  const float postX[2] = {-g.halfWidth, g.halfWidth};
  const float postY[2] = {-g.goalHalfWidth, g.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      football::contact::CapsuleCollider post;
      post.tipA = blunted::Vector3(x, y, 0.0f);
      post.tipB = blunted::Vector3(x, y, g.goalHeight);
      post.radius = g.postRadius;
      consider(post);
    }
  }
  const float barX[2] = {-g.halfWidth, g.halfWidth};
  for (float x : barX) {
    football::contact::CapsuleCollider bar;
    bar.tipA = blunted::Vector3(x, -g.goalHalfWidth, g.goalHeight);
    bar.tipB = blunted::Vector3(x, g.goalHalfWidth, g.goalHeight);
    bar.radius = g.postRadius;
    consider(bar);
  }
  return earliest;
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

    // ---- CCD loop (7G-8-b/c): find the earliest impact among static
    // obstacles (ground plane + woodwork), resolve it, advance to it, and
    // repeat until the tick's time is consumed. -------
    float remaining = dt;
    constexpr int kMaxContactIterations = 8;
    for (int iteration = 0;
         iteration < kMaxContactIterations && remaining > 0.0f; ++iteration) {
      const blunted::Vector3 end = next.position + next.velocity * remaining;

      std::optional<football::contact::BallContact> earliest;
      float earliestTime = remaining;

      // Ground plane. Penetrating -> t=0 correction; otherwise a falling
      // ball reaches the plane when its centre is at z == radius.
      const float groundGap = next.position.coords[2] - ball.radius;
      if (groundGap < 0.0f) {
        football::contact::BallContact ground;
        ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
        ground.point = next.position.Get2D();
        ground.penetration = -groundGap;
        ground.timeWithinTick = 0.0f;
        earliest = ground;
        earliestTime = 0.0f;
      } else if (next.velocity.coords[2] < 0.0f) {
        const float t = groundGap / -next.velocity.coords[2];
        football::contact::BallContact ground;
        ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
        ground.point = (next.position + next.velocity * t).Get2D();
        ground.penetration = 0.0f;
        ground.timeWithinTick = t;
        earliest = ground;
        earliestTime = t;
      }

      // Woodwork: swept sphere against each static capsule.
      if (apply_woodwork) {
        if (auto wood = SweepBallWoodworkContact(
                next.position, end, ball, goal, remaining)) {
          if (wood->timeWithinTick < earliestTime) {
            earliest = wood;
            earliestTime = wood->timeWithinTick;
          }
        }
      }

      if (!earliest) {
        next.position += next.velocity * remaining;
        remaining = 0.0f;
        break;
      }

      // Advance to the impact instant, then resolve impulse and correct
      // position independently of the impulse.
      next.position += next.velocity * earliest->timeWithinTick;
      football::contact::ContactMaterial material;
      football::contact::BallContactResolver::Resolve(next, ball, *earliest,
                                                       material);
      next.position += earliest->normal * earliest->penetration;
      remaining -= earliest->timeWithinTick;
    }

    // Ground persistent dynamics (7G-7d-c): sliding -> rolling -> rest.
    const GroundDynamicsParams groundParams;
    if (next.position.coords[2] <= ball.radius + groundParams.contactTolerance) {
      GroundDynamics::Step(next, dt, ball, groundParams);
    }

    // Orientation integration.
    BallDynamics::IntegrateOrientation(next, dt);

    return next;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_PHYSICS
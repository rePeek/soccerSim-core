#ifndef _HPP_CORE_PHYSICS_BALL_PHYSICS
#define _HPP_CORE_PHYSICS_BALL_PHYSICS

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/state/ball_state.hpp"
#include "core/state/player_id.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/physics/ball_dynamics.hpp"
#include "core/contact/ball_contact.hpp"
#include "core/contact/ball_contact_resolver.hpp"
#include "core/contact/ball_ground_contact.hpp"
#include "core/contact/capsule_collider.hpp"
#include "core/contact/player_body_collider.hpp"
#include "core/contact/sphere_capsule_contact.hpp"
#include "core/contact/sweep_sphere_capsule.hpp"
#include "core/physics/ground_dynamics.hpp"



// 7G-8-d-c: where a resolved contact came from. Rules/possession layers use
// this to attribute a touch without physics knowing anything about teams.
enum class BallContactSourceType {
  Ground,
  Woodwork,
  PlayerBody,
};

struct BallContactSource {
  BallContactSourceType type = BallContactSourceType::Ground;
  PlayerId player = kInvalidPlayerId;  // only meaningful for PlayerBody
};

struct BallImpact {
  football::contact::BallContact contact;
  BallContactSource source;
};

// A passive body that can collide with the ball during a physics tick. The
// body is captured at tick start; `contactMargin` is a migration-compatible
// carry-over of the legacy boundingBoxSizeOffset and will disappear later.
struct PlayerBodyCandidate {
  PlayerId player = kInvalidPlayerId;
  football::contact::PlayerBodyCollider body;
  blunted::Vector3 surfaceVelocity = blunted::Vector3(0);
  float contactMargin = 0.0f;
};

struct BallPhysicsStepResult {
  BallState state;
  std::array<BallImpact, 8> impacts;
  uint8_t impactCount = 0;
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

// 7G-8-d-a: sweep the ball against one moving body candidate. The capsule trio
// is translated to the current elapsed position and the ball sweeps the
// relative chord (ballVelocity - surfaceVelocity) over the remaining time.
inline std::optional<football::contact::BallContact> SweepPlayerBodyCandidate(
    const PlayerBodyCandidate &candidate, const blunted::Vector3 &ballPosition,
    const blunted::Vector3 &ballVelocity, float effectiveRadius, float elapsed,
    float remaining) {
  const blunted::Vector3 shift = candidate.surfaceVelocity * elapsed;
  const blunted::Vector3 relativeEnd =
      ballPosition + (ballVelocity - candidate.surfaceVelocity) * remaining;
  const football::contact::CapsuleCollider *volumes[3] = {
      &candidate.body.upperBody, &candidate.body.lowerBody, &candidate.body.head};
  std::optional<football::contact::BallContact> earliest;
  for (const football::contact::CapsuleCollider *volume : volumes) {
    football::contact::CapsuleCollider shifted;
    shifted.tipA = volume->tipA + shift;
    shifted.tipB = volume->tipB + shift;
    shifted.radius = volume->radius;
    if (auto c = football::contact::SweepSphereCapsule(
            ballPosition, relativeEnd, effectiveRadius, shifted, remaining)) {
      if (!earliest || c->timeWithinTick < earliest->timeWithinTick) {
        earliest = c;
      }
    }
  }
  return earliest;
}

// Single-step ball integration with a CCD time axis and source attribution.
// Ground, woodwork and the passed-in moving player bodies all compete for the
// earliest time of impact; resolved contacts are returned as BallImpact list.
struct BallPhysics {
  static BallPhysicsStepResult Step(
      const BallState& current, float dt,
      const football::domain::BallProfile& ball, bool apply_woodwork,
      const GoalGeometry& goal,
      const std::vector<PlayerBodyCandidate>& players) {
    BallPhysicsStepResult result;
    result.state = current;

    // ---- Free flight: acceleration (velocity + angular velocity). ----
    BallDynamics::ApplyForces(result.state, dt, ball, BallDynamicsParams());

    // ---- CCD loop: earliest impact among ground, woodwork and moving
    // player bodies; advance, resolve relative to the surface, repeat.
    float remaining = dt;
    float elapsed = 0.0f;
    constexpr int kMaxContacts = 8;
    constexpr int kPriorityGround = 0;
    constexpr int kPriorityWoodwork = 1;
    constexpr int kPriorityPlayer = 2;
    for (int iteration = 0;
         iteration < kMaxContacts && remaining > 0.0f &&
         result.impactCount < kMaxContacts; ++iteration) {
      const blunted::Vector3 end =
          result.state.position + result.state.velocity * remaining;

      bool hasEarliest = false;
      float earliestTime = remaining;
      int earliestPriority = 999;
      football::contact::BallContact earliestContact;
      BallContactSource earliestSource;
      blunted::Vector3 earliestSurfaceVelocity(0.0f, 0.0f, 0.0f);

      constexpr float kTieEpsilon = 1e-6f;
      auto consider = [&](const football::contact::BallContact &c,
                           const BallContactSource &source,
                           const blunted::Vector3 &surfaceVelocity,
                           int priority) {
        const float diff = c.timeWithinTick - earliestTime;
        const bool earlier = diff < -kTieEpsilon;
        const bool tie = std::fabs(diff) <= kTieEpsilon;
        const bool betterPriority = tie && priority < earliestPriority;
        const bool betterPlayer =
            tie && priority == earliestPriority &&
            priority == kPriorityPlayer && source.player < earliestSource.player;
        if (!hasEarliest || earlier || betterPriority || betterPlayer) {
          earliestContact = c;
          earliestSource = source;
          earliestSurfaceVelocity = surfaceVelocity;
          earliestTime = c.timeWithinTick;
          earliestPriority = priority;
          hasEarliest = true;
        }
      };

      // Ground plane (static).
      {
        BallContactSource source;
        source.type = BallContactSourceType::Ground;
        const float groundGap = result.state.position.coords[2] - ball.radius;
        if (groundGap < 0.0f) {
          football::contact::BallContact ground;
          ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
          ground.point = result.state.position.Get2D();
          ground.penetration = -groundGap;
          ground.timeWithinTick = 0.0f;
          consider(ground, source, blunted::Vector3(0), kPriorityGround);
        } else if (result.state.velocity.coords[2] < 0.0f) {
          const float t = groundGap / -result.state.velocity.coords[2];
          football::contact::BallContact ground;
          ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
          ground.point =
              (result.state.position + result.state.velocity * t).Get2D();
          ground.penetration = 0.0f;
          ground.timeWithinTick = t;
          consider(ground, source, blunted::Vector3(0), kPriorityGround);
        }
      }

      // Woodwork (static capsules).
      if (apply_woodwork) {
        BallContactSource source;
        source.type = BallContactSourceType::Woodwork;
        if (auto wood = SweepBallWoodworkContact(
                result.state.position, end, ball, goal, remaining)) {
          consider(*wood, source, blunted::Vector3(0), kPriorityWoodwork);
        }
      }

      // Moving player bodies.
      for (const PlayerBodyCandidate &candidate : players) {
        BallContactSource source;
        source.type = BallContactSourceType::PlayerBody;
        source.player = candidate.player;
        const float effectiveRadius = ball.radius + candidate.contactMargin;
        if (auto hit = SweepPlayerBodyCandidate(
                candidate, result.state.position, result.state.velocity,
                effectiveRadius, elapsed, remaining)) {
          consider(*hit, source, candidate.surfaceVelocity, kPriorityPlayer);
        }
      }

      if (!hasEarliest) {
        result.state.position += result.state.velocity * remaining;
        remaining = 0.0f;
        break;
      }

      // Advance to the impact, resolve against the moving surface, then
      // correct position independently of the impulse.
      result.state.position +=
          result.state.velocity * earliestContact.timeWithinTick;
      football::contact::ContactMaterial material;
      football::contact::BallContactResolver::Resolve(
          result.state, ball, earliestContact, earliestSurfaceVelocity,
          material);
      result.state.position +=
          earliestContact.normal * earliestContact.penetration;

      BallImpact impact;
      impact.contact = earliestContact;
      impact.contact.timeWithinTick = elapsed + earliestContact.timeWithinTick;
      impact.source = earliestSource;
      result.impacts[result.impactCount++] = impact;

      elapsed += earliestContact.timeWithinTick;
      remaining -= earliestContact.timeWithinTick;
    }

    // Ground persistent dynamics (7G-7d-c): sliding -> rolling -> rest.
    const GroundDynamicsParams groundParams;
    if (result.state.position.coords[2] <= ball.radius + groundParams.contactTolerance) {
      GroundDynamics::Step(result.state, dt, ball, groundParams);
    }

    // Orientation integration.
    BallDynamics::IntegrateOrientation(result.state, dt);

    return result;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_PHYSICS
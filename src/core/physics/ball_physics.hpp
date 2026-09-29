#ifndef FOOTBALL_CORE_PHYSICS_BALL_PHYSICS_HPP
#define FOOTBALL_CORE_PHYSICS_BALL_PHYSICS_HPP

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/contact/ball_contact.hpp"
#include "core/contact/ball_contact_resolver.hpp"
#include "core/contact/capsule_collider.hpp"
#include "core/contact/player_body_collider.hpp"
#include "core/contact/sphere_capsule_contact.hpp"
#include "core/contact/sweep_sphere_capsule.hpp"
#include "core/model/ball/ball.hpp"
#include "core/model/player/player.hpp"
#include "core/physics/ball_dynamics.hpp"
#include "core/physics/ground_dynamics.hpp"

namespace football_sim::physics {

// Rules and possession consume this attribution without physics knowing teams
// or Match. Player identity is part of Player's immutable profile.
enum class BallContactSourceType { Ground, Woodwork, PlayerBody };

struct BallContactSource {
  BallContactSourceType type = BallContactSourceType::Ground;
  football_sim::PlayerId player = football_sim::kInvalidPlayerId;
};

struct BallImpact {
  football_sim::contact::BallContact contact;
  BallContactSource source;
};

// Captured at tick start. The player model owns its identity; the collision
// candidate carries only derived geometry and surface motion for this tick.
struct PlayerBodyCandidate {
  football_sim::PlayerId player = football_sim::kInvalidPlayerId;
  football_sim::contact::PlayerBodyCollider body;
  football_sim::math::Vector3 surfaceVelocity = football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  float contactMargin = 0.0f;
};

struct BallPhysicsStepResult {
  std::array<BallImpact, 8> impacts{};
  uint8_t impactCount = 0;
};

struct GoalGeometry {
  float halfWidth = 55.0f;
  float goalHalfWidth = 3.7f;
  float goalHeight = 2.5f;
  float postRadius = 0.07f;
  float postAbsorbInv = 0.8f;
};

inline std::optional<football_sim::contact::BallContact> DetectBallWoodworkContact(
    const football_sim::Ball& ball, const GoalGeometry& geometry) {
  const football_sim::BallState& state = ball.State();
  const float postX[2] = {-geometry.halfWidth, geometry.halfWidth};
  const float postY[2] = {-geometry.goalHalfWidth, geometry.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      football_sim::contact::CapsuleCollider post;
      post.tipA = football_sim::math::Vector3(x, y, 0.0f);
      post.tipB = football_sim::math::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      if (auto contact = football_sim::contact::DetectSphereCapsuleContact(
              state.position, ball.Radius(), post)) {
        return contact;
      }
    }
  }
  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    football_sim::contact::CapsuleCollider bar;
    bar.tipA = football_sim::math::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB = football_sim::math::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    if (auto contact = football_sim::contact::DetectSphereCapsuleContact(
            state.position, ball.Radius(), bar)) {
      return contact;
    }
  }
  return std::nullopt;
}

inline std::optional<football_sim::contact::BallContact> SweepBallWoodworkContact(
    const football_sim::math::Vector3& start, const football_sim::math::Vector3& end,
    const football_sim::Ball& ball, const GoalGeometry& geometry, float dt) {
  std::optional<football_sim::contact::BallContact> earliest;
  const auto consider = [&](const football_sim::contact::CapsuleCollider& capsule) {
    if (auto contact = football_sim::contact::SweepSphereCapsule(
            start, end, ball.Radius(), capsule, dt)) {
      if (!earliest || contact->timeWithinTick < earliest->timeWithinTick) {
        earliest = contact;
      }
    }
  };

  const float postX[2] = {-geometry.halfWidth, geometry.halfWidth};
  const float postY[2] = {-geometry.goalHalfWidth, geometry.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      football_sim::contact::CapsuleCollider post;
      post.tipA = football_sim::math::Vector3(x, y, 0.0f);
      post.tipB = football_sim::math::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      consider(post);
    }
  }
  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    football_sim::contact::CapsuleCollider bar;
    bar.tipA = football_sim::math::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB = football_sim::math::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    consider(bar);
  }
  return earliest;
}

inline std::optional<football_sim::contact::BallContact> SweepPlayerBodyCandidate(
    const PlayerBodyCandidate& candidate, const football_sim::math::Vector3& ballPosition,
    const football_sim::math::Vector3& ballVelocity, float effectiveRadius, float elapsed,
    float remaining) {
  const football_sim::math::Vector3 shift = candidate.surfaceVelocity * elapsed;
  const football_sim::math::Vector3 relativeEnd =
      ballPosition + (ballVelocity - candidate.surfaceVelocity) * remaining;
  const football_sim::contact::CapsuleCollider* volumes[3] = {
      &candidate.body.upperBody, &candidate.body.lowerBody, &candidate.body.head};
  std::optional<football_sim::contact::BallContact> earliest;
  for (const football_sim::contact::CapsuleCollider* volume : volumes) {
    football_sim::contact::CapsuleCollider shifted;
    shifted.tipA = volume->tipA + shift;
    shifted.tipB = volume->tipB + shift;
    shifted.radius = volume->radius;
    if (auto contact = football_sim::contact::SweepSphereCapsule(
            ballPosition, relativeEnd, effectiveRadius, shifted, remaining)) {
      if (!earliest || contact->timeWithinTick < earliest->timeWithinTick) {
        earliest = contact;
      }
    }
  }
  return earliest;
}

// Authoritative 10 ms ball integration. Prediction uses a copied Ball and an
// empty player-candidate list, preserving its pure ball/ground/woodwork meaning.
struct BallPhysics {
  static BallPhysicsStepResult Step(
      football_sim::Ball& ball, float dt, bool applyWoodwork,
      const GoalGeometry& goal,
      const std::vector<PlayerBodyCandidate>& players = {}) {
    BallPhysicsStepResult result;
    BallDynamics::ApplyForces(ball, dt);

    float remaining = dt;
    float elapsed = 0.0f;
    constexpr int kMaxContacts = 8;
    constexpr int kPriorityGround = 0;
    constexpr int kPriorityWoodwork = 1;
    constexpr int kPriorityPlayer = 2;

    for (int iteration = 0;
         iteration < kMaxContacts && remaining > 0.0f &&
         result.impactCount < kMaxContacts;
         ++iteration) {
      const football_sim::BallState current = ball.State();
      const football_sim::math::Vector3 end = current.position + current.velocity * remaining;
      bool hasEarliest = false;
      float earliestTime = remaining;
      int earliestPriority = 999;
      football_sim::contact::BallContact earliestContact;
      BallContactSource earliestSource;
      football_sim::math::Vector3 earliestSurfaceVelocity(0.0f, 0.0f, 0.0f);

      constexpr float kTieEpsilon = 1e-6f;
      const auto consider = [&](const football_sim::contact::BallContact& contact,
                                const BallContactSource& source,
                                const football_sim::math::Vector3& surfaceVelocity,
                                int priority) {
        const float difference = contact.timeWithinTick - earliestTime;
        const bool earlier = difference < -kTieEpsilon;
        const bool tie = std::fabs(difference) <= kTieEpsilon;
        const bool betterPriority = tie && priority < earliestPriority;
        const bool betterPlayer =
            tie && priority == earliestPriority && priority == kPriorityPlayer &&
            source.player < earliestSource.player;
        if (!hasEarliest || earlier || betterPriority || betterPlayer) {
          earliestContact = contact;
          earliestSource = source;
          earliestSurfaceVelocity = surfaceVelocity;
          earliestTime = contact.timeWithinTick;
          earliestPriority = priority;
          hasEarliest = true;
        }
      };

      {
        BallContactSource source;
        source.type = BallContactSourceType::Ground;
        const float groundGap = current.position.coords[2] - ball.Radius();
        if (groundGap < 0.0f) {
          football_sim::contact::BallContact ground;
          ground.normal = football_sim::math::Vector3(0.0f, 0.0f, 1.0f);
          ground.point = current.position.Get2D();
          ground.penetration = -groundGap;
          consider(ground, source, football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                   kPriorityGround);
        } else if (current.velocity.coords[2] < 0.0f) {
          const float time = groundGap / -current.velocity.coords[2];
          if (time <= remaining) {
            football_sim::contact::BallContact ground;
            ground.normal = football_sim::math::Vector3(0.0f, 0.0f, 1.0f);
            ground.point =
                (current.position + current.velocity * time).Get2D();
            ground.timeWithinTick = time;
            consider(ground, source, football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                     kPriorityGround);
          }
        }
      }

      if (applyWoodwork) {
        BallContactSource source;
        source.type = BallContactSourceType::Woodwork;
        if (auto contact =
                SweepBallWoodworkContact(current.position, end, ball, goal, remaining)) {
          consider(*contact, source, football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                   kPriorityWoodwork);
        }
      }

      for (const PlayerBodyCandidate& candidate : players) {
        BallContactSource source;
        source.type = BallContactSourceType::PlayerBody;
        source.player = candidate.player;
        if (auto contact = SweepPlayerBodyCandidate(
                candidate, current.position, current.velocity,
                ball.Radius() + candidate.contactMargin, elapsed, remaining)) {
          consider(*contact, source, candidate.surfaceVelocity, kPriorityPlayer);
        }
      }

      if (!hasEarliest) {
        football_sim::BallState next = current;
        next.position += next.velocity * remaining;
        ball.SetState(next);
        remaining = 0.0f;
        break;
      }

      football_sim::BallState atImpact = current;
      atImpact.position += atImpact.velocity * earliestContact.timeWithinTick;
      ball.SetState(atImpact);
      football_sim::contact::ContactMaterial material;
      football_sim::contact::BallContactResolver::Resolve(
          ball, earliestContact, earliestSurfaceVelocity, material);

      football_sim::BallState corrected = ball.State();
      corrected.position += earliestContact.normal * earliestContact.penetration;
      ball.SetState(corrected);

      BallImpact impact;
      impact.contact = earliestContact;
      impact.contact.timeWithinTick = elapsed + earliestContact.timeWithinTick;
      impact.source = earliestSource;
      result.impacts[result.impactCount++] = impact;
      elapsed += earliestContact.timeWithinTick;
      remaining -= earliestContact.timeWithinTick;
    }

    const GroundDynamicsParams ground;
    if (ball.State().position.coords[2] <=
        ball.Radius() + ground.contactTolerance) {
      GroundDynamics::Step(ball, dt, ground);
    }
    BallDynamics::IntegrateOrientation(ball, dt);
    return result;
  }
};

}  // namespace football_sim::physics
#endif  // FOOTBALL_CORE_PHYSICS_BALL_PHYSICS_HPP

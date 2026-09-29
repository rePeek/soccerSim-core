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

// Rules and possession consume this attribution without physics knowing teams
// or Match. Player identity is part of Player's immutable profile.
enum class BallContactSourceType { Ground, Woodwork, PlayerBody };

struct BallContactSource {
  BallContactSourceType type = BallContactSourceType::Ground;
  football::model::PlayerId player = football::model::kInvalidPlayerId;
};

struct BallImpact {
  football::contact::BallContact contact;
  BallContactSource source;
};

// Captured at tick start. The player model owns its identity; the collision
// candidate carries only derived geometry and surface motion for this tick.
struct PlayerBodyCandidate {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  football::contact::PlayerBodyCollider body;
  blunted::Vector3 surfaceVelocity = blunted::Vector3(0.0f, 0.0f, 0.0f);
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

inline std::optional<football::contact::BallContact> DetectBallWoodworkContact(
    const football::model::Ball& ball, const GoalGeometry& geometry) {
  const football::model::BallState& state = ball.State();
  const float postX[2] = {-geometry.halfWidth, geometry.halfWidth};
  const float postY[2] = {-geometry.goalHalfWidth, geometry.goalHalfWidth};
  for (float x : postX) {
    for (float y : postY) {
      football::contact::CapsuleCollider post;
      post.tipA = blunted::Vector3(x, y, 0.0f);
      post.tipB = blunted::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      if (auto contact = football::contact::DetectSphereCapsuleContact(
              state.position, ball.Radius(), post)) {
        return contact;
      }
    }
  }
  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    football::contact::CapsuleCollider bar;
    bar.tipA = blunted::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB = blunted::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    if (auto contact = football::contact::DetectSphereCapsuleContact(
            state.position, ball.Radius(), bar)) {
      return contact;
    }
  }
  return std::nullopt;
}

inline std::optional<football::contact::BallContact> SweepBallWoodworkContact(
    const blunted::Vector3& start, const blunted::Vector3& end,
    const football::model::Ball& ball, const GoalGeometry& geometry, float dt) {
  std::optional<football::contact::BallContact> earliest;
  const auto consider = [&](const football::contact::CapsuleCollider& capsule) {
    if (auto contact = football::contact::SweepSphereCapsule(
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
      football::contact::CapsuleCollider post;
      post.tipA = blunted::Vector3(x, y, 0.0f);
      post.tipB = blunted::Vector3(x, y, geometry.goalHeight);
      post.radius = geometry.postRadius;
      consider(post);
    }
  }
  const float barX[2] = {-geometry.halfWidth, geometry.halfWidth};
  for (float x : barX) {
    football::contact::CapsuleCollider bar;
    bar.tipA = blunted::Vector3(x, -geometry.goalHalfWidth, geometry.goalHeight);
    bar.tipB = blunted::Vector3(x, geometry.goalHalfWidth, geometry.goalHeight);
    bar.radius = geometry.postRadius;
    consider(bar);
  }
  return earliest;
}

inline std::optional<football::contact::BallContact> SweepPlayerBodyCandidate(
    const PlayerBodyCandidate& candidate, const blunted::Vector3& ballPosition,
    const blunted::Vector3& ballVelocity, float effectiveRadius, float elapsed,
    float remaining) {
  const blunted::Vector3 shift = candidate.surfaceVelocity * elapsed;
  const blunted::Vector3 relativeEnd =
      ballPosition + (ballVelocity - candidate.surfaceVelocity) * remaining;
  const football::contact::CapsuleCollider* volumes[3] = {
      &candidate.body.upperBody, &candidate.body.lowerBody, &candidate.body.head};
  std::optional<football::contact::BallContact> earliest;
  for (const football::contact::CapsuleCollider* volume : volumes) {
    football::contact::CapsuleCollider shifted;
    shifted.tipA = volume->tipA + shift;
    shifted.tipB = volume->tipB + shift;
    shifted.radius = volume->radius;
    if (auto contact = football::contact::SweepSphereCapsule(
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
      football::model::Ball& ball, float dt, bool applyWoodwork,
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
      const football::model::BallState current = ball.State();
      const blunted::Vector3 end = current.position + current.velocity * remaining;
      bool hasEarliest = false;
      float earliestTime = remaining;
      int earliestPriority = 999;
      football::contact::BallContact earliestContact;
      BallContactSource earliestSource;
      blunted::Vector3 earliestSurfaceVelocity(0.0f, 0.0f, 0.0f);

      constexpr float kTieEpsilon = 1e-6f;
      const auto consider = [&](const football::contact::BallContact& contact,
                                const BallContactSource& source,
                                const blunted::Vector3& surfaceVelocity,
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
          football::contact::BallContact ground;
          ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
          ground.point = current.position.Get2D();
          ground.penetration = -groundGap;
          consider(ground, source, blunted::Vector3(0.0f, 0.0f, 0.0f),
                   kPriorityGround);
        } else if (current.velocity.coords[2] < 0.0f) {
          const float time = groundGap / -current.velocity.coords[2];
          if (time <= remaining) {
            football::contact::BallContact ground;
            ground.normal = blunted::Vector3(0.0f, 0.0f, 1.0f);
            ground.point =
                (current.position + current.velocity * time).Get2D();
            ground.timeWithinTick = time;
            consider(ground, source, blunted::Vector3(0.0f, 0.0f, 0.0f),
                     kPriorityGround);
          }
        }
      }

      if (applyWoodwork) {
        BallContactSource source;
        source.type = BallContactSourceType::Woodwork;
        if (auto contact =
                SweepBallWoodworkContact(current.position, end, ball, goal, remaining)) {
          consider(*contact, source, blunted::Vector3(0.0f, 0.0f, 0.0f),
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
        football::model::BallState next = current;
        next.position += next.velocity * remaining;
        ball.SetState(next);
        remaining = 0.0f;
        break;
      }

      football::model::BallState atImpact = current;
      atImpact.position += atImpact.velocity * earliestContact.timeWithinTick;
      ball.SetState(atImpact);
      football::contact::ContactMaterial material;
      football::contact::BallContactResolver::Resolve(
          ball, earliestContact, earliestSurfaceVelocity, material);

      football::model::BallState corrected = ball.State();
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

#endif  // FOOTBALL_CORE_PHYSICS_BALL_PHYSICS_HPP

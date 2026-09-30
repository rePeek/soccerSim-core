#include "ball_collision_solver.hpp"

#include <algorithm>
#include <cmath>

#include "core/physics/contact/ball/ball_contact_resolver.hpp"
#include "core/physics/contact/ball/ground_dynamics.hpp"
#include "core/physics/contact/ball/woodwork_contact.hpp"

namespace football_sim::contact {

BallContactStepResult SolveBallContacts(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    float dt, bool applyWoodwork, const GoalGeometry& goal,
    const std::vector<PlayerBodyCandidate>& players) {
  BallContactStepResult result;
  result.state = state;

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
    const football_sim::BallState& current = result.state;
    const football_sim::math::Vector3 end =
        current.position + current.velocity * remaining;
    bool hasEarliest = false;
    float earliestTime = remaining;
    int earliestPriority = 999;
    BallContact earliestContact;
    BallContactSource earliestSource;
    football_sim::math::Vector3 earliestSurfaceVelocity(0.0f, 0.0f, 0.0f);

    constexpr float kTieEpsilon = 1e-6f;
    const auto consider = [&](const BallContact& contact,
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

    // Ground plane (static).
    {
      BallContactSource source;
      source.type = BallContactSourceType::Ground;
      const float groundGap = current.position.coords[2] - ball.Radius();
      if (groundGap < 0.0f) {
        BallContact ground;
        ground.normal = football_sim::math::Vector3(0.0f, 0.0f, 1.0f);
        ground.point = current.position.Get2D();
        ground.penetration = -groundGap;
        consider(ground, source, football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                 kPriorityGround);
      } else if (current.velocity.coords[2] < 0.0f) {
        const float time = groundGap / -current.velocity.coords[2];
        if (time <= remaining) {
          BallContact ground;
          ground.normal = football_sim::math::Vector3(0.0f, 0.0f, 1.0f);
          ground.point = (current.position + current.velocity * time).Get2D();
          ground.timeWithinTick = time;
          consider(ground, source,
                   football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                   kPriorityGround);
        }
      }
    }

    // Woodwork (static capsules).
    if (applyWoodwork) {
      BallContactSource source;
      source.type = BallContactSourceType::Woodwork;
      if (auto contact = SweepBallWoodworkContact(current.position, end, ball,
                                                  goal, remaining)) {
        consider(*contact, source,
                 football_sim::math::Vector3(0.0f, 0.0f, 0.0f),
                 kPriorityWoodwork);
      }
    }

    // Moving player bodies.
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
      result.state.position += result.state.velocity * remaining;
      remaining = 0.0f;
      break;
    }

    // Advance to the impact, resolve against the moving surface, then correct
    // position independently of the impulse.
    result.state.position +=
        result.state.velocity * earliestContact.timeWithinTick;
    const ContactMaterial material;
    result.state =
        ResolveBallContact(result.state, ball, earliestContact,
                           earliestSurfaceVelocity, material);
    result.state.position += earliestContact.normal * earliestContact.penetration;

    BallImpact impact;
    impact.contact = earliestContact;
    impact.contact.timeWithinTick = elapsed + earliestContact.timeWithinTick;
    impact.source = earliestSource;
    result.impacts[result.impactCount++] = impact;

    elapsed += earliestContact.timeWithinTick;
    remaining -= earliestContact.timeWithinTick;
  }

  // Persistent ground contact: sliding -> rolling -> rest.
  const GroundDynamicsParams ground;
  if (result.state.position.coords[2] <=
      ball.Radius() + ground.contactTolerance) {
    result.state = ApplyGroundContact(result.state, ball, dt, ground);
  }

  return result;
}

}  // namespace football_sim::contact
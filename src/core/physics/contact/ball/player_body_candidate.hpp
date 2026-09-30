#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_PLAYER_BODY_CANDIDATE_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_PLAYER_BODY_CANDIDATE_HPP

#include <optional>

#include "core/model/player/player.hpp"
#include "core/physics/contact/ball/ball_contact.hpp"
#include "core/physics/contact/geometry/capsule_collider.hpp"
#include "core/physics/contact/geometry/player_body_collider.hpp"
#include "core/physics/contact/geometry/sweep_sphere_capsule.hpp"

namespace football_sim::contact {

// Captured at tick start. The player model owns its identity; the collision
// candidate carries only derived geometry and surface motion for this tick.
struct PlayerBodyCandidate {
  football_sim::PlayerId player = football_sim::kInvalidPlayerId;
  PlayerBodyCollider body;
  football_sim::math::Vector3 surfaceVelocity =
      football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  float contactMargin = 0.0f;
};

// Sweeps the ball against one moving body candidate. The capsule trio is
// translated to the current elapsed position and the ball sweeps the relative
// chord (ballVelocity - surfaceVelocity) over the remaining time.
inline std::optional<BallContact> SweepPlayerBodyCandidate(
    const PlayerBodyCandidate& candidate,
    const football_sim::math::Vector3& ballPosition,
    const football_sim::math::Vector3& ballVelocity, float effectiveRadius,
    float elapsed, float remaining) {
  const football_sim::math::Vector3 shift = candidate.surfaceVelocity * elapsed;
  const football_sim::math::Vector3 relativeEnd =
      ballPosition + (ballVelocity - candidate.surfaceVelocity) * remaining;
  const CapsuleCollider* volumes[3] = {&candidate.body.upperBody,
                                       &candidate.body.lowerBody,
                                       &candidate.body.head};
  std::optional<BallContact> earliest;
  for (const CapsuleCollider* volume : volumes) {
    CapsuleCollider shifted;
    shifted.tipA = volume->tipA + shift;
    shifted.tipB = volume->tipB + shift;
    shifted.radius = volume->radius;
    if (auto contact = SweepSphereCapsule(ballPosition, relativeEnd,
                                          effectiveRadius, shifted, remaining)) {
      if (!earliest || contact->timeWithinTick < earliest->timeWithinTick) {
        earliest = contact;
      }
    }
  }
  return earliest;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_PLAYER_BODY_CANDIDATE_HPP

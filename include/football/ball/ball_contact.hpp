#ifndef FOOTBALL_BALL_BALL_CONTACT_HPP
#define FOOTBALL_BALL_BALL_CONTACT_HPP

#include <optional>
#include <span>
#include <vector>

#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_state.hpp"
#include "football/foundation/geometry/collider.hpp"
#include "football/pitch/pitch.hpp"

namespace football::ball { class BallModel; }

namespace football::ball {

// Evidence of one ball contact, produced by Ball from a
// football::collision::CollisionHit and completed with response data. `toi` is
// the first contact time within [0, 1]; `normal` points from the collider
// surface toward the ball, so an approaching ball has (ball.velocity -
// collider.velocity) dot normal < 0.
struct BallContact {
  football::geometry::ColliderId collider = 0;
  float toi = 0.0f;
  football::math::Vector3 point; // ball center at TOI; projected center after penetration repair
  football::math::Vector3 normal;
  football::math::Vector3 relative_velocity;
  // Populated only by AdvanceBallTick/Step, not by the collision query.
  float normal_impulse = 0.0f; // >0 identifies an impulse-bearing impact
  bool position_corrected = false; // correction is not itself a new touch
  // Surface velocity at TOI, including minimal-twist capsule rotation/stretch
  // and radial dilation (see football::geometry::ColliderMotion). Not one endpoint's velocity.
  football::math::Vector3 surface_velocity = football::math::Vector3(0);
};


// One step of free motion (constant velocity) with collision response and
// remainder integration. Pure and deterministic; gravity/friction/spin remain
// higher-level BallPhysics layers. Reflection uses the given restitution; a
// TOI<=0 contact is treated as resting to avoid an infinite contact loop.
struct BallStepResult {
  BallState state;
  std::vector<BallContact> contacts;
  // P5b: the active endpoint impulse actually applied this tick, if any. It is
  // applied after passive motion and never advances position, so
  // there is at most one active impulse per tick.
  std::optional<BallImpulse> active_impulse;
  std::optional<BallEndpointConstraint> endpoint_constraint;
};


// Tick-based model: one free-motion candidate and at most one impulse-bearing
// impact at the earliest TOI. A contact that only projects the ball out of an
// existing overlap is instantaneous: it is reported with a zero normal impulse
// and does not consume the tick, so the ball keeps its free motion and may
// still find one genuine impact (P4e). Grounded is a persistent rolling
// constraint (z = radius, vz = 0, horizontal roll + rolling drag), not a
// per-tick ground impact. Response reads football::geometry::ColliderMotion::material.
//
// All long-lived physical parameters come from the ball's own model and the
// pitch it moves in; this call only adds the colliders active in this tick.
BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const football::geometry::ColliderMotion> colliders,
                               float dt,
                               const football::ball::BallModel& config,
                               const football::pitch::Pitch& pitch);

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_CONTACT_HPP

#ifndef FOOTBALL_BALL_BALL_CONTACT_HPP
#define FOOTBALL_BALL_BALL_CONTACT_HPP

#include <optional>
#include <span>
#include <vector>

#include "football/ball/ball_state.hpp"
#include "football/ball/collider.hpp"

namespace football::model { class BallConfig; }

namespace football::ball {

// Result of one continuous collision detection query. `toi` is the first
// contact time within [0, 1]; `normal` points from the collider surface toward
// the ball so an approaching ball has (ball.velocity - collider.velocity) dot
// normal < 0.
struct BallContact {
  ColliderId collider = 0;
  float toi = 0.0f;
  blunted::Vector3 point;
  blunted::Vector3 normal;
  blunted::Vector3 relative_velocity;
};

// Pure swept-sphere CCD against one collider over one tick. Never mutates state
// and draws no RNG. `dt` is seconds; `ball_radius` is the football radius.
std::optional<BallContact> SweepBall(const BallState& ball,
                                     const ColliderMotion& collider,
                                     float dt, float ball_radius);

std::optional<BallContact> FirstContact(const BallState& ball,
                                        std::span<const ColliderMotion> colliders,
                                        float dt, float ball_radius);

// One step of free motion (constant velocity) with collision response and
// remainder integration. Pure and deterministic; gravity/friction/spin remain
// higher-level BallPhysics layers. Reflection uses the given restitution; a
// TOI<=0 contact is treated as resting to avoid an infinite contact loop.
struct BallStepResult {
  BallState state;
  std::vector<BallContact> contacts;
};

BallStepResult AdvanceBall(const BallState& initial,
                           std::span<const ColliderMotion> colliders,
                           float dt, float ball_radius, float restitution,
                           std::size_t max_contacts = 8);

// Continuous forces for the single-tick kernel. The exact legacy coefficient
// extraction (Magnus, spin quaternion integration) is P3c; these leave the
// explicit execution points, not yet the production numeric model.
struct BallDynamics {
  float gravity = 9.81f;
  float quadratic_resistance = 0.04f;
  float ground_deceleration = 1.6f;
  float grass_height = 0.025f;
  float spin_decay = 0.0f;
  float magnus_coefficient = 0.0f;
};

// Tick-based model: one free-motion candidate, at most one new impact at the
// earliest TOI (no remainder integration). Grounded is a persistent rolling
// constraint (z = radius, vz = 0, horizontal roll + rolling drag), not a
// per-tick ground impact. Response reads ColliderMotion::material.
BallStepResult AdvanceBallTick(const BallState& initial,
                               std::span<const ColliderMotion> colliders,
                               float dt,
                               const football::model::BallConfig& config,
                               const BallDynamics& dynamics);

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_CONTACT_HPP

#ifndef FOOTBALL_BALL_BALL_CONTACT_HPP
#define FOOTBALL_BALL_BALL_CONTACT_HPP

#include <optional>
#include <span>

#include "football/ball/ball_state.hpp"
#include "football/ball/collider.hpp"

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

// Earliest contact across colliders; simultaneous hits are broken by ColliderId
// (deterministic and independent of input order).
std::optional<BallContact> FirstContact(const BallState& ball,
                                        std::span<const ColliderMotion> colliders,
                                        float dt, float ball_radius);

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_CONTACT_HPP

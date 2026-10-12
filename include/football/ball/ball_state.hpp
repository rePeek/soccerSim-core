#ifndef FOOTBALL_BALL_BALL_STATE_HPP
#define FOOTBALL_BALL_BALL_STATE_HPP

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::ball {

// Single observable snapshot of the ball's physical state.
//
// Units:
//   position          meters (world frame)
//   velocity          meters / second
//   angular_velocity  radians / second around each world axis
//   orientation       unit quaternion
//
// This value type owns no derived data (prediction cache, last-touch history
// or competition state). It is the only state shape visible through the stable
// Ball API, in SI units (m, m/s, rad/s, unit quaternion).
struct BallState {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
  football::math::Vector3 angular_velocity;
  football::math::Quaternion orientation;
};

// Kinematic endpoint constraint, not an impact. No football identity/rules.
struct BallEndpointConstraint {
  football::math::Vector3 position;
  football::math::Vector3 velocity;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_STATE_HPP

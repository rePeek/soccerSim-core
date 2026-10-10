#ifndef FOOTBALL_BALL_BALL_STATE_HPP
#define FOOTBALL_BALL_BALL_STATE_HPP

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

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
// Ball API; transitional compatibility accessors still project the legacy
// per-millisecond rotation quaternion onto `angular_velocity`.
struct BallState {
  blunted::Vector3 position;
  blunted::Vector3 velocity;
  blunted::Vector3 angular_velocity;
  blunted::Quaternion orientation;
};

// Kinematic endpoint constraint, not an impact. No football identity/rules.
struct BallEndpointConstraint {
  blunted::Vector3 position;
  blunted::Vector3 velocity;
};

}  // namespace football::ball

#endif  // FOOTBALL_BALL_BALL_STATE_HPP

#ifndef FOOTBALL_BALL_BALL_PHYSICS_HPP
#define FOOTBALL_BALL_BALL_PHYSICS_HPP

// Private to football::ball. Simulation/Player/Referee/Observation must never
// include this header: it is the implementation detail behind Ball::Step and
// Ball::Predict.

#include "football/ball/ball_config.hpp"
#include "football/ball/ball_environment.hpp"
#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"
#include "sim/time/tick.hpp"

namespace football::ball::detail {

// The legacy authoritative physical frame, kept as-is so the cohesion phase
// reproduces the historical floating-point execution order exactly. The
// public BallState projects onto this; `rotation_ms` stores the legacy
// per-millisecond rotation quaternion, not the rad/s angular velocity.
struct PhysicsState {
  blunted::Vector3 position;
  blunted::Vector3 momentum;
  blunted::Quaternion rotation_ms;
  blunted::Quaternion orientation;
};

// Advances one simulation tick (kTickSeconds) preserving the exact legacy
// computation order for gravity, drag, ground friction, bounce, woodwork,
// netting, ground-rotation coupling and the Magnus swerve.
//
// `first_step` reproduces the legacy "first prediction step" gating for
// woodwork and netting collisions.
PhysicsState Advance(const PhysicsState& current,
                     const football::ball::BallConfig& config,
                     const football::model::Pitch& pitch,
                     const football::ball::BallEnvironment& environment,
                     bool first_step);

}  // namespace football::ball::detail

#endif  // FOOTBALL_BALL_BALL_PHYSICS_HPP

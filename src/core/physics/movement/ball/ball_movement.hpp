#ifndef FOOTBALL_CORE_PHYSICS_MOVEMENT_BALL_BALL_MOVEMENT_HPP
#define FOOTBALL_CORE_PHYSICS_MOVEMENT_BALL_BALL_MOVEMENT_HPP

#include "core/model/ball/ball.hpp"

namespace football_sim::physics::movement::ball {

// Physical-model knobs. `gravity` is signed (negative points down along +Z);
// the remaining values are standard sea-level-air sphere coefficients.
struct BallMovementParameters {
  float gravity = -9.81f;
  float airDensity = 1.225f;
  float dragCoefficient = 0.47f;
  float liftCoefficient = 0.2f;
  float spinDamping = 0.5f;
};

// Pure accelerations. They read state and immutable ball attributes and return
// a vector; they never mutate Ball, World or any other simulation state.

[[nodiscard]] football_sim::math::Vector3 GravityAcceleration(
    const BallMovementParameters& parameters);

[[nodiscard]] football_sim::math::Vector3 DragAcceleration(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    const BallMovementParameters& parameters);

[[nodiscard]] football_sim::math::Vector3 MagnusAcceleration(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    const BallMovementParameters& parameters);

// Applies force-sourced acceleration to velocity and angular velocity.
// Position and orientation are deliberately left untouched so contact solving
// can run between the force step and positional integration.
[[nodiscard]] football_sim::BallState ApplyForces(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    float dt, const BallMovementParameters& parameters);

// Free-flight positional advance: position += velocity * dt.
[[nodiscard]] football_sim::BallState IntegratePosition(
    const football_sim::BallState& state, float dt);

// Advances the accumulated orientation by the world-space spin vector.
[[nodiscard]] football_sim::BallState IntegrateOrientation(
    const football_sim::BallState& state, float dt);

}  // namespace football_sim::physics::movement::ball

#endif  // FOOTBALL_CORE_PHYSICS_MOVEMENT_BALL_BALL_MOVEMENT_HPP
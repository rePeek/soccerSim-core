#include "ball_movement.hpp"

#include <cassert>
#include <cmath>

namespace football_sim::physics::movement::ball {

namespace {

[[nodiscard]] float CrossSection(const football_sim::Ball& ball) {
  return football_sim::math::pi * ball.Radius() * ball.Radius();
}

}  // namespace

football_sim::math::Vector3 GravityAcceleration(
    const BallMovementParameters& parameters) {
  return football_sim::math::Vector3(0.0f, 0.0f, parameters.gravity);
}

football_sim::math::Vector3 DragAcceleration(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    const BallMovementParameters& parameters) {
  const float speed = state.velocity.GetLength();
  if (speed <= 0.0f) {
    return football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  }
  // F_d = -0.5 rho Cd A |v| v, divided by mass to get acceleration.
  const float factor = -0.5f * parameters.airDensity *
                       parameters.dragCoefficient * CrossSection(ball) * speed /
                       ball.Mass();
  return state.velocity * factor;
}

football_sim::math::Vector3 MagnusAcceleration(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    const BallMovementParameters& parameters) {
  const float speed = state.velocity.GetLength();
  if (speed <= 0.0f) {
    return football_sim::math::Vector3(0.0f, 0.0f, 0.0f);
  }
  // F_M = 0.5 rho Cl A R (omega x v), divided by mass.
  const float factor = 0.5f * parameters.airDensity *
                       parameters.liftCoefficient * CrossSection(ball) *
                       ball.Radius() / ball.Mass();
  return state.angularVelocity.GetCrossProduct(state.velocity) * factor;
}

football_sim::BallState ApplyForces(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    float dt, const BallMovementParameters& parameters) {
  assert(dt > 0.0f);
  assert(ball.Mass() > 0.0f);

  football_sim::BallState next = state;
  const football_sim::math::Vector3 acceleration =
      GravityAcceleration(parameters) + DragAcceleration(state, ball, parameters) +
      MagnusAcceleration(state, ball, parameters);

  next.velocity += acceleration * dt;
  next.angularVelocity -= state.angularVelocity * (parameters.spinDamping * dt);
  return next;
}

football_sim::BallState IntegratePosition(const football_sim::BallState& state,
                                          float dt) {
  football_sim::BallState next = state;
  next.position += state.velocity * dt;
  return next;
}

football_sim::BallState IntegrateOrientation(const football_sim::BallState& state,
                                             float dt) {
  football_sim::BallState next = state;
  const float omega = state.angularVelocity.GetLength();
  constexpr float kSpinEpsilon = 1e-6f;
  if (omega > kSpinEpsilon) {
    const football_sim::math::Vector3 axis = state.angularVelocity / omega;
    football_sim::math::Quaternion deltaRotation;
    deltaRotation.SetAngleAxis(omega * dt, axis);
    next.orientation = deltaRotation * state.orientation;
    next.orientation.Normalize();
  }
  return next;
}

}  // namespace football_sim::physics::movement::ball
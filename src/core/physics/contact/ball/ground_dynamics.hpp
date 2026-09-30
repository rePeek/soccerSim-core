#ifndef FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GROUND_DYNAMICS_HPP
#define FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GROUND_DYNAMICS_HPP

#include <cassert>
#include <cmath>

#include "core/model/ball/ball.hpp"

namespace football_sim::contact {

struct GroundDynamicsParams {
  float gravityMagnitude = 9.81f;
  float slideFriction = 0.4f;
  float rollingResistance = 0.02f;
  float restSpeedThreshold = 0.2f;
  float noSlipEpsilon = 0.05f;
  float contactTolerance = 0.025f;
};

// Persistent ground contact: sliding -> rolling -> rest. Pure function over
// the given state; owner commit happens later in the tick.
//
//   vc          = v + omega x r          (r = (0,0,-R))
//   tangential  = vc - n * dot(vc, n)    (slipping velocity at the contact)
//
//   sliding     |tangential| > noSlipEpsilon : Coulomb friction decelerates
//                                           translation and torques the ball.
//   rolling     |tangential| <= noSlipEpsilon : no-slip v = omega x -r with
//                                           rolling resistance.
//   rest        horizontal speed <= restSpeedThreshold : stop.
[[nodiscard]] inline football_sim::BallState ApplyGroundContact(
    const football_sim::BallState& state, const football_sim::Ball& ball,
    float dt, const GroundDynamicsParams& params = {}) {
  assert(dt > 0.0f);
  assert(ball.Radius() > 0.0f);
  assert(ball.InertiaFactor() > 0.0f);

  football_sim::BallState next = state;
  const float radius = ball.Radius();
  const float tangentialX =
      state.velocity.coords[0] - state.angularVelocity.coords[1] * radius;
  const float tangentialY =
      state.velocity.coords[1] + state.angularVelocity.coords[0] * radius;
  const float tangentialSpeed =
      std::sqrt(tangentialX * tangentialX + tangentialY * tangentialY);
  const float horizontalSpeed =
      std::sqrt(state.velocity.coords[0] * state.velocity.coords[0] +
                state.velocity.coords[1] * state.velocity.coords[1]);

  if (horizontalSpeed <= params.restSpeedThreshold &&
      tangentialSpeed <= params.noSlipEpsilon) {
    next.velocity.coords[0] = 0.0f;
    next.velocity.coords[1] = 0.0f;
    next.angularVelocity.coords[0] = 0.0f;
    next.angularVelocity.coords[1] = 0.0f;
    return next;
  }

  if (tangentialSpeed > params.noSlipEpsilon) {
    const float tx = tangentialX / tangentialSpeed;
    const float ty = tangentialY / tangentialSpeed;
    const float frictionAccel = params.slideFriction * params.gravityMagnitude;
    next.velocity.coords[0] -= frictionAccel * tx * dt;
    next.velocity.coords[1] -= frictionAccel * ty * dt;

    const float alpha =
        frictionAccel / (ball.InertiaFactor() * ball.Radius());
    next.angularVelocity.coords[0] -= alpha * ty * dt;
    next.angularVelocity.coords[1] += alpha * tx * dt;
  } else {
    const float hx = state.velocity.coords[0];
    const float hy = state.velocity.coords[1];
    const float speed = std::sqrt(hx * hx + hy * hy);
    if (speed > params.restSpeedThreshold) {
      const float ux = hx / speed;
      const float uy = hy / speed;
      const float resistance =
          params.rollingResistance * params.gravityMagnitude;
      next.velocity.coords[0] -= resistance * ux * dt;
      next.velocity.coords[1] -= resistance * uy * dt;
    }
    next.angularVelocity.coords[0] = -state.velocity.coords[1] / radius;
    next.angularVelocity.coords[1] = state.velocity.coords[0] / radius;
  }
  return next;
}

}  // namespace football_sim::contact

#endif  // FOOTBALL_CORE_PHYSICS_CONTACT_BALL_GROUND_DYNAMICS_HPP
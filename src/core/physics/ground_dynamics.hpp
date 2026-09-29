#ifndef _HPP_CORE_PHYSICS_GROUND_DYNAMICS
#define _HPP_CORE_PHYSICS_GROUND_DYNAMICS

#include <cassert>
#include <cmath>

#include "core/model/ball/ball_profile.hpp"
#include "core/model/ball/ball.hpp"

// 7G-7d-c: persistent ground dynamics for a ball already in contact with the
// plane. This is the complement of the impact resolver: it models the
// sustained contact between bounces, not the instantaneous collision impulse.
//
// For a ball on the plane (n = +Z, r = (0,0,-R)):
//   vc          = v + omega x r
//   tangential  = vc - n * dot(vc, n)   (the slipping velocity at the contact)
//
//   sliding     |tangential| > noSlipEpsilon
//               Coulomb friction decelerates translation and torques the ball
//               toward rolling.
//   rolling     |tangential| <= noSlipEpsilon
//               no-slip v = omega x -r, with rolling resistance decelerating.
//   rest        horizontal speed <= restSpeedThreshold
//               horizontal motion and rolling spin stop.
//
// Only velocity and angularVelocity are mutated; position and orientation are
// left to the caller's integration.
struct GroundDynamicsParams {
  float gravityMagnitude = 9.81f;   // |g|
  float slideFriction = 0.4f;       // Coulomb sliding friction (dimensionless)
  float rollingResistance = 0.02f;  // rolling resistance (dimensionless)
  float restSpeedThreshold = 0.2f;  // m/s
  float noSlipEpsilon = 0.05f;      // m/s tangential slip tolerance
  float contactTolerance = 0.025f;  // ball bottom within this of the plane
};

struct GroundDynamics {
  static void Step(BallState &state, float dt,
                   const football::model::BallProfile &ball,
                   const GroundDynamicsParams &params) {
    DO_VALIDATION;
    assert(dt > 0.0f);
    assert(ball.radius > 0.0f);
    assert(ball.inertiaFactor > 0.0f);

    const float invRadius = 1.0f / ball.radius;
    const float tangentialX = state.velocity.coords[0] -
                              state.angularVelocity.coords[1] * ball.radius;
    const float tangentialY = state.velocity.coords[1] +
                              state.angularVelocity.coords[0] * ball.radius;
    const float tangentialSpeed =
        std::sqrt(tangentialX * tangentialX + tangentialY * tangentialY);

    const float horizontalSpeed = std::sqrt(
        state.velocity.coords[0] * state.velocity.coords[0] +
        state.velocity.coords[1] * state.velocity.coords[1]);

    if (horizontalSpeed <= params.restSpeedThreshold &&
        tangentialSpeed <= params.noSlipEpsilon) {
      state.velocity.coords[0] = 0.0f;
      state.velocity.coords[1] = 0.0f;
      state.angularVelocity.coords[0] = 0.0f;
      state.angularVelocity.coords[1] = 0.0f;
      return;
    }

    if (tangentialSpeed > params.noSlipEpsilon) {
      // Sliding: Coulomb friction opposes the slip and torques the ball.
      const float tx = tangentialX / tangentialSpeed;
      const float ty = tangentialY / tangentialSpeed;
      const float frictionAccel = params.slideFriction * params.gravityMagnitude;
      state.velocity.coords[0] -= frictionAccel * tx * dt;
      state.velocity.coords[1] -= frictionAccel * ty * dt;

      const float alpha = frictionAccel / (ball.inertiaFactor * ball.radius);
      state.angularVelocity.coords[0] -= alpha * ty * dt;
      state.angularVelocity.coords[1] += alpha * tx * dt;
    } else {
      // Rolling: rolling resistance, then re-impose no-slip spin.
      const float hx = state.velocity.coords[0];
      const float hy = state.velocity.coords[1];
      const float hSpeed = std::sqrt(hx * hx + hy * hy);
      if (hSpeed > params.restSpeedThreshold) {
        const float ux = hx / hSpeed;
        const float uy = hy / hSpeed;
        const float resistAccel =
            params.rollingResistance * params.gravityMagnitude;
        state.velocity.coords[0] -= resistAccel * ux * dt;
        state.velocity.coords[1] -= resistAccel * uy * dt;
      }
      state.angularVelocity.coords[0] = -state.velocity.coords[1] * invRadius;
      state.angularVelocity.coords[1] = state.velocity.coords[0] * invRadius;
    }
  }
};

#endif  // _HPP_CORE_PHYSICS_GROUND_DYNAMICS
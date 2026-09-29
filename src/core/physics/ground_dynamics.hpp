#ifndef FOOTBALL_CORE_PHYSICS_GROUND_DYNAMICS_HPP
#define FOOTBALL_CORE_PHYSICS_GROUND_DYNAMICS_HPP

#include <cassert>
#include <cmath>

#include "core/model/ball/ball.hpp"

struct GroundDynamicsParams {
  float gravityMagnitude = 9.81f;
  float slideFriction = 0.4f;
  float rollingResistance = 0.02f;
  float restSpeedThreshold = 0.2f;
  float noSlipEpsilon = 0.05f;
  float contactTolerance = 0.025f;
};

// Persistent ground contact (sliding -> rolling -> rest). It computes the
// whole next BallState before committing it through the model.
struct GroundDynamics {
  static void Step(football::model::Ball& ball, float dt,
                   const GroundDynamicsParams& params = {}) {
    assert(dt > 0.0f);
    assert(ball.Radius() > 0.0f);
    assert(ball.InertiaFactor() > 0.0f);

    football::model::BallState next = ball.State();
    const float radius = ball.Radius();
    const float tangentialX =
        next.velocity.coords[0] - next.angularVelocity.coords[1] * radius;
    const float tangentialY =
        next.velocity.coords[1] + next.angularVelocity.coords[0] * radius;
    const float tangentialSpeed =
        std::sqrt(tangentialX * tangentialX + tangentialY * tangentialY);
    const float horizontalSpeed = std::sqrt(
        next.velocity.coords[0] * next.velocity.coords[0] +
        next.velocity.coords[1] * next.velocity.coords[1]);

    if (horizontalSpeed <= params.restSpeedThreshold &&
        tangentialSpeed <= params.noSlipEpsilon) {
      next.velocity.coords[0] = 0.0f;
      next.velocity.coords[1] = 0.0f;
      next.angularVelocity.coords[0] = 0.0f;
      next.angularVelocity.coords[1] = 0.0f;
      ball.SetState(next);
      return;
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
      const float hx = next.velocity.coords[0];
      const float hy = next.velocity.coords[1];
      const float speed = std::sqrt(hx * hx + hy * hy);
      if (speed > params.restSpeedThreshold) {
        const float ux = hx / speed;
        const float uy = hy / speed;
        const float resistance =
            params.rollingResistance * params.gravityMagnitude;
        next.velocity.coords[0] -= resistance * ux * dt;
        next.velocity.coords[1] -= resistance * uy * dt;
      }
      next.angularVelocity.coords[0] = -next.velocity.coords[1] / radius;
      next.angularVelocity.coords[1] = next.velocity.coords[0] / radius;
    }
    ball.SetState(next);
  }
};

#endif  // FOOTBALL_CORE_PHYSICS_GROUND_DYNAMICS_HPP

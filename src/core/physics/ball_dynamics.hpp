#ifndef FOOTBALL_CORE_PHYSICS_BALL_DYNAMICS_HPP
#define FOOTBALL_CORE_PHYSICS_BALL_DYNAMICS_HPP

#include <cassert>
#include <cmath>

#include "core/model/ball/ball.hpp"

struct BallDynamicsParams {
  float gravity = -9.81f;
  float airDensity = 1.225f;
  float dragCoefficient = 0.47f;
  float liftCoefficient = 0.2f;
  float spinDamping = 0.5f;
};

// Standard rigid-ball free-flight dynamics. The object remains thin: all
// calculations are here and the completed state is committed through Ball.
struct BallDynamics {
  static void ApplyForces(football::model::Ball& ball, float dt,
                          const BallDynamicsParams& params = {}) {
    assert(dt > 0.0f);
    assert(ball.Mass() > 0.0f);

    football::model::BallState next = ball.State();
    const float invMass = 1.0f / ball.Mass();
    const float crossSection = blunted::pi * ball.Radius() * ball.Radius();
    blunted::Vector3 acceleration(0.0f, 0.0f, params.gravity);

    const float speed = next.velocity.GetLength();
    if (speed > 0.0f) {
      acceleration += next.velocity *
                      (-0.5f * params.airDensity * params.dragCoefficient *
                       crossSection * speed * invMass);
      acceleration +=
          next.angularVelocity.GetCrossProduct(next.velocity) *
          (0.5f * params.airDensity * params.liftCoefficient * crossSection *
           ball.Radius() * invMass);
    }

    next.velocity += acceleration * dt;
    next.angularVelocity -= next.angularVelocity * (params.spinDamping * dt);
    ball.SetState(next);
  }

  static void IntegrateOrientation(football::model::Ball& ball, float dt) {
    football::model::BallState next = ball.State();
    const float omega = next.angularVelocity.GetLength();
    constexpr float kSpinEpsilon = 1e-6f;
    if (omega > kSpinEpsilon) {
      const blunted::Vector3 axis = next.angularVelocity / omega;
      blunted::Quaternion deltaRotation;
      deltaRotation.SetAngleAxis(omega * dt, axis);
      next.orientation = deltaRotation * next.orientation;
      next.orientation.Normalize();
      ball.SetState(next);
    }
  }
};

#endif  // FOOTBALL_CORE_PHYSICS_BALL_DYNAMICS_HPP

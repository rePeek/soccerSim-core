#ifndef _HPP_CORE_PHYSICS_BALL_DYNAMICS
#define _HPP_CORE_PHYSICS_BALL_DYNAMICS

#include <cassert>
#include <cmath>

#include "core/state/ball_state.hpp"
#include "core/domain/ball/ball_profile.hpp"

// 7G-7b: standard rigid-ball free-flight dynamics, re-implemented from the
// standard equations rather than the legacy magic-number swerve:
//
//   gravity      a = (0, 0, g)
//   drag         a = -0.5 rho Cd A |v| v / m
//   Magnus       a =  0.5 rho Cl A r (w x v) / m
//   angular drag w -= w * k * dt            (viscous spin decay)
//
// Semi-implicit Euler is used for velocity/angular-velocity and orientation;
// position integration stays in BallPhysics::Step so contact response can
// still clamp/reflect the position before it is committed.
//
// These are textbook equations (Reynolds-drag, Kutta-Joukowski Magnus and
// viscous torque), not a port of any external codebase.
struct BallDynamicsParams {
  float gravity = -9.81f;         // m/s^2
  float airDensity = 1.225f;      // kg/m^3
  float dragCoefficient = 0.47f;  // sphere
  float liftCoefficient = 0.2f;   // Magnus lift (tuning knob)
  float angularDrag = 0.5f;       // viscous angular drag (1/s)
};

struct BallDynamics {
  // Accelerations only: updates velocity and angularVelocity. Position and
  // orientation are deliberately left untouched so contact response can run
  // between the force step and the integration step.
  static void ApplyForces(BallState &state, float dt,
                          const football::domain::BallProfile &ball,
                          const BallDynamicsParams &params) {
    DO_VALIDATION;
    assert(dt > 0.0f);
    assert(ball.mass > 0.0f);

    const float invMass = 1.0f / ball.mass;
    const float crossSection = blunted::pi * ball.radius * ball.radius;

    blunted::Vector3 acceleration(0.0f, 0.0f, params.gravity);

    const float speed = state.velocity.GetLength();
    if (speed > 0.0f) {
      // Quadratic aerodynamic drag opposes the velocity.
      acceleration += state.velocity *
                      (-0.5f * params.airDensity * params.dragCoefficient *
                       crossSection * speed * invMass);

      // Magnus lift is perpendicular to both spin and velocity.
      acceleration +=
          state.angularVelocity.GetCrossProduct(state.velocity) *
          (0.5f * params.airDensity * params.liftCoefficient * crossSection *
           ball.radius * invMass);
    }

    state.velocity += acceleration * dt;
    state.angularVelocity -=
        state.angularVelocity * (params.angularDrag * dt);
  }

  // Advances the accumulated orientation by the current angular velocity.
  static void IntegrateOrientation(BallState &state, float dt) {
    DO_VALIDATION;
    const blunted::Vector3 deltaAxis = state.angularVelocity * dt;
    blunted::Quaternion deltaRotation;
    deltaRotation.SetAngles(deltaAxis.coords[0], deltaAxis.coords[1],
                            deltaAxis.coords[2]);
    state.orientation = deltaRotation * state.orientation;
  }
};

#endif  // _HPP_CORE_PHYSICS_BALL_DYNAMICS
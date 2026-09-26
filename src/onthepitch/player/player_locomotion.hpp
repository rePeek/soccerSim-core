//
//  player_locomotion.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_LOCOMOTION
#define _HPP_PLAYER_LOCOMOTION

#include <cmath>

#include "../../defines.hpp"
#include "player_kinematics.hpp"

// Simulation-owned procedural locomotion for pure Movement ticks.
//
// This is deliberately NOT a re-implementation of the animation root motion.
// The legacy measurements in tools/football_regression.cpp established which
// structures are worth keeping and which are animation artifacts:
//
//   kept:
//     - locomotion facing is the velocity direction, not an independently
//       turned look direction (legacy directionVec equals the movement
//       direction bit-exactly whenever the quantized velocity is not idle);
//     - speeding up and slowing down are strongly asymmetric: legacy stops far
//       more abruptly than it accelerates;
//     - turning costs speed, and turning is harder at higher speed.
//
//   dropped:
//     - a 1.36 rad heading change within one tick, which only existed because a
//       particular animation happened to allow it. Turning is rate limited
//       here and paid for with speed instead.
//
// The velocity class (idle/dribble/walk/sprint) is an animation-selection
// regime, not a physical quantity, so nothing in this model snaps the speed to
// four discrete values. Class-dependent behaviour belongs in the parameter
// profiles (acceleration, braking, turn penalty), which callers may vary.
struct PlayerLocomotionInput {
  // World-space desired velocity. Its length is the desired speed, its 2D
  // direction is the desired locomotion direction.
  Vector3 desiredVelocity = Vector3(0);
  // Facing to hold while standing still. Locomotion facing is the velocity
  // direction while moving, so this only applies below the idle threshold.
  Vector3 idleFacing = Vector3(0, -1, 0);
};

struct PlayerLocomotionParameters {
  float maxSpeed = 7.5f;
  float acceleration = 6.0f;  // m/s^2 while speeding up
  float braking = 12.0f;      // m/s^2 while slowing down; asymmetric on purpose
  float maxTurnRate = 6.0f;   // rad/s at a standstill
  float turnRateSpeedFactor = 0.75f;  // 1 == no turning authority at max speed
  float turnSpeedLoss = 0.5f;  // fraction of target speed lost at pi rad error
  float idleSpeedThreshold = 0.5f;  // at or below this the actor is standing
};

class PlayerLocomotion {
 public:
  // Moves one step of dt seconds. Planar only: the z coordinate of position,
  // velocity and facing is always forced back to 0.
  static void Step(PlayerKinematicState &state,
                   const PlayerLocomotionInput &input,
                   const PlayerLocomotionParameters &parameters, float dt) {
    DO_VALIDATION;
    assert(dt > 0.0f);
    assert(parameters.maxSpeed > 0.0f);
    assert(parameters.acceleration > 0.0f);
    assert(parameters.braking > 0.0f);
    assert(parameters.maxTurnRate >= 0.0f);

    const Vector3 previousFacing =
        state.facing.Get2D().GetNormalized(Vector3(0, -1, 0));
    const Vector3 desiredVelocity = input.desiredVelocity.Get2D();
    const float desiredSpeed = desiredVelocity.GetLength();
    const float currentSpeed = state.velocity.GetLength();
    const Vector3 currentDirection =
        state.velocity.Get2D().GetNormalized(previousFacing);
    const Vector3 desiredDirection =
        desiredVelocity.GetNormalized(currentDirection);

    // Turning costs speed. The error is only meaningful while actually moving;
    // at a standstill the actor may simply start in any direction.
    const bool moving = currentSpeed > parameters.idleSpeedThreshold;
    const float directionError =
        moving ? std::fabs(currentDirection.GetAngle2D(desiredDirection)) : 0.0f;
    const float turnPenalty =
        1.0f - parameters.turnSpeedLoss * (directionError / pi);
    const float targetSpeed =
        clamp(desiredSpeed, 0.0f, parameters.maxSpeed) * turnPenalty;

    const float newSpeed = ApproachAsymmetric(currentSpeed, targetSpeed,
                                              parameters.acceleration,
                                              parameters.braking, dt);

    // Harder to turn at speed. At a standstill the full rate applies so a
    // stationary actor can start in the desired direction instead of driving
    // off along its old facing.
    const float speedRatio =
        clamp(currentSpeed / parameters.maxSpeed, 0.0f, 1.0f);
    const float turnAuthority =
        parameters.maxTurnRate *
        (1.0f - parameters.turnRateSpeedFactor * speedRatio);
    const float requestedTurn =
        currentDirection.GetAngle2D(desiredDirection);
    const float appliedTurn =
        clamp(requestedTurn, -turnAuthority * dt, turnAuthority * dt);
    const Vector3 newDirection =
        currentDirection.GetRotated2D(appliedTurn).GetNormalized(currentDirection);

    state.velocity = newDirection * newSpeed;
    state.velocity.coords[2] = 0.0f;
    state.speed = newSpeed;

    if (newSpeed > parameters.idleSpeedThreshold) {
      state.facing = newDirection;
    } else {
      state.facing = input.idleFacing.Get2D().GetNormalized(previousFacing);
    }
    state.facing.coords[2] = 0.0f;

    state.position += state.velocity * dt;
    state.position.coords[2] = 0.0f;
  }

  // Speed approach with separate acceleration and braking rates. Returns the
  // target exactly rather than overshooting it.
  static float ApproachAsymmetric(float current, float target,
                                  float acceleration, float braking, float dt) {
    DO_VALIDATION;
    const float rate = target < current ? braking : acceleration;
    const float maxStep = rate * dt;
    const float delta = target - current;
    if (std::fabs(delta) <= maxStep) return target;
    return current + (delta > 0.0f ? maxStep : -maxStep);
  }

  // Roll the same Step() forward. Planning must use the physics primitive
  // directly: as soon as a planner has its own acceleration or turn rule, the
  // execution and the plan describe different motions and the closed loop
  // diverges. The input is held constant, which is the straight-run case.
  static PlayerKinematicState Predict(PlayerKinematicState state,
                                      const PlayerLocomotionInput &input,
                                      const PlayerLocomotionParameters &parameters,
                                      int time_ms) {
    DO_VALIDATION;
    for (int elapsed = 0; elapsed < time_ms; elapsed += 10) {
      Step(state, input, parameters, 0.01f);
    }
    return state;
  }

  // How long until this actor can be within reach_radius of the target if it
  // runs at desired_speed and re-aims every tick. Returns -1 when the target
  // is not reached inside horizon_ms. This is the reachability counterpart of
  // Predict() and deliberately calls the same Step().
  static int EstimateTimeToTarget(const PlayerKinematicState &start,
                                  const Vector3 &target,
                                  const PlayerLocomotionParameters &parameters,
                                  float desired_speed, int horizon_ms,
                                  float reach_radius) {
    DO_VALIDATION;
    if (desired_speed <= 0.0f) return -1;
    PlayerKinematicState state = start;
    for (int elapsed = 0; elapsed < horizon_ms; elapsed += 10) {
      const Vector3 to_target = (target - state.position).Get2D();
      if (to_target.GetLength() <= reach_radius) return elapsed;
      PlayerLocomotionInput input;
      input.desiredVelocity =
          to_target.GetNormalized(state.facing) * desired_speed;
      input.idleFacing = state.facing;
      Step(state, input, parameters, 0.01f);
    }
    const Vector3 remaining = (target - state.position).Get2D();
    return remaining.GetLength() <= reach_radius ? horizon_ms : -1;
  }
};

#endif

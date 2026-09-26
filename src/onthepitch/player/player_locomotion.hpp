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

// Dual estimate of when an actor gets inside a reach radius. The two radii are
// the legacy AI's usual (can actually touch the ball) and optimistic distances,
// so replacing TimeNeeded keeps its dual-estimate semantics. A negative value
// means "not reached inside the horizon".
// Reach distances matching the legacy AI's dual estimate: the usual radius is
// "close enough to actually touch the ball", the optimistic one is looser.
constexpr float kLocomotionUsualReachRadius = 0.28f;
constexpr float kLocomotionOptimisticReachRadius = 0.9f;

struct PlayerLocomotionReach {
  int usual_ms = -1;
  int optimistic_ms = -1;
};

// Diagnostics only, never simulation state and never serialized: counts how
// many earliest-intercept solves have run, so the regression can report the
// planner's real cost and cadence instead of estimating them.
inline int &PlayerLocomotionInterceptSolverCalls() {
  static int calls = 0;
  return calls;
}

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
    // Vector3::GetAngle2D() and Vector3::GetRotated2D() use opposite sign
    // conventions, so the angle must be measured from the desired direction
    // back to the current one. Measuring it the other way rotates the actor
    // away from its target and it settles into a circling equilibrium with a
    // permanent ~pi heading error, which halves its speed through the turn
    // penalty and makes it look unreachable to any planner.
    const float requestedTurn = desiredDirection.GetAngle2D(currentDirection);
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

  // How long until this actor is inside each reach radius if it commits to
  // desired_speed and re-aims at the fixed target every tick. The two radii
  // mirror the legacy AI's usual (0.28, can actually touch the ball) and
  // optimistic (0.90) distances so the dual estimate stays available. A
  // negative value means the radius was not reached inside horizon_ms.
  //
  // This is a capability estimate, not a prediction of the command the
  // controller will actually emit: it answers "if this actor chases as hard as
  // the locomotion physics allow, when can it get there".
  static PlayerLocomotionReach EstimateArrival(
      const PlayerKinematicState &start, const Vector3 &target,
      const PlayerLocomotionParameters &parameters, float desired_speed,
      int horizon_ms, float usual_radius, float optimistic_radius) {
    DO_VALIDATION;
    PlayerLocomotionReach reach;
    if (desired_speed <= 0.0f) return reach;
    PlayerKinematicState state = start;
    for (int elapsed = 0; elapsed <= horizon_ms; elapsed += 10) {
      const float distance =
          (target.Get2D() - state.position).GetLength();
      if (reach.optimistic_ms < 0 && distance <= optimistic_radius) {
        reach.optimistic_ms = elapsed;
      }
      if (reach.usual_ms < 0 && distance <= usual_radius) {
        reach.usual_ms = elapsed;
      }
      // The optimistic radius is the looser one, so it is always satisfied
      // first and the usual radius ends the rollout.
      if (reach.usual_ms >= 0 || elapsed == horizon_ms) break;
      PlayerLocomotionInput input;
      input.desiredVelocity =
          (target.Get2D() - state.position).GetNormalized(state.facing) *
          desired_speed;
      input.idleFacing = state.facing;
      Step(state, input, parameters, 0.01f);
    }
    return reach;
  }

  // The AI's question is "when can I first intercept a moving ball", and the
  // legacy code answers it as candidate-time reachability: for every candidate
  // interception time, take the ball position predicted for that moment and
  // ask whether the actor could be there by then. The earliest candidate that
  // works is the interception time.
  //
  // Pure pursuit (re-aiming at the ball's current predicted position each
  // tick) is NOT the same question: it chases the ball's tail and reports a
  // moving ball as far less reachable than it is, which is what made an
  // earlier version of this estimator report 99% of balls as unreachable.
  template <typename TargetAtTime>
  static PlayerLocomotionReach EstimateEarliestIntercept(
      const PlayerKinematicState &start, TargetAtTime target_at,
      const PlayerLocomotionParameters &parameters, float desired_speed,
      int horizon_ms, float usual_radius, float optimistic_radius) {
    DO_VALIDATION;
    ++PlayerLocomotionInterceptSolverCalls();
    PlayerLocomotionReach intercept;
    if (desired_speed <= 0.0f) return intercept;
    // Exact reachability bound. Step() never adds more than
    // acceleration * dt to the speed and never exceeds
    // max(initial speed, min(desired speed, maxSpeed)), so in n steps the
    // actor can cover at most dMax(n) metres. A candidate farther away than
    // dMax(n) + radius therefore cannot be reached inside its horizon, for
    // either radius, so skipping it cannot change the answer. This is much
    // tighter than a linear desired_speed * t bound because it accounts for the
    // acceleration ramp, which is what makes the solver O(N^2) otherwise.
    const float dt = 0.01f;
    const float speed_ceiling =
        std::max(start.velocity.GetLength(),
                 std::min(desired_speed, parameters.maxSpeed));
    float bound_speed = start.velocity.GetLength();
    float bound_distance = 0.0f;
    for (int intercept_ms = 0; intercept_ms <= horizon_ms;
         intercept_ms += 10) {
      const Vector3 intercept_point = target_at(intercept_ms).Get2D();
      // A hair of slack keeps the bound conservative under rounding, which only
      // ever weakens the pruning and never prunes a reachable candidate.
      if (intercept_ms > 0 &&
          (intercept_point - start.position).GetLength() >
              bound_distance + optimistic_radius + 1e-3f) {
        bound_speed = std::min(speed_ceiling,
                               bound_speed + parameters.acceleration * dt);
        bound_distance += bound_speed * dt;
        continue;
      }
      const PlayerLocomotionReach arrival =
          EstimateArrival(start, intercept_point, parameters, desired_speed,
                          intercept_ms, usual_radius, optimistic_radius);
      if (intercept.optimistic_ms < 0 && arrival.optimistic_ms >= 0) {
        intercept.optimistic_ms = intercept_ms;
      }
      if (intercept.usual_ms < 0 && arrival.usual_ms >= 0) {
        intercept.usual_ms = intercept_ms;
      }
      if (intercept.usual_ms >= 0) break;
      bound_speed = std::min(speed_ceiling,
                             bound_speed + parameters.acceleration * dt);
      bound_distance += bound_speed * dt;
    }
    return intercept;
  }
};

#endif

// Copyright 2026
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef _HPP_PLAYER_BODY_FACING
#define _HPP_PLAYER_BODY_FACING

#include "sim/player/player_kinematics.hpp"

// Simulation-owned torso/orientation model. It is intentionally separate from
// PlayerLocomotion: locomotion writes position, velocity and facing; this model
// consumes the resulting facing and writes only bodyFacing.
struct PlayerBodyFacingInput {
  Vector3 desiredFacing = Vector3(0, -1, 0);
};

// H3e3b-prep5 selection from the event-based shadow grid. 6 rad/s gives a
// 90-degree torso response in about 262 ms; 90 degrees permits side-looking
// without making a long-lived backwards torso pose a valid target.
constexpr float kBodyFacingMaxTurnRate = 6.0f;
constexpr float kBodyFacingMaxRelativeAngle = pi * 0.5f;
struct PlayerBodyFacingParameters {
  float maxTurnRate = kBodyFacingMaxTurnRate;       // radians per second
  float maxRelativeAngle = kBodyFacingMaxRelativeAngle;  // radians from facing
};

class PlayerBodyFacing {
 public:
  // Clamp the requested torso direction into the cone around locomotion facing.
  // GetAngle2D and GetRotated2D have opposite sign conventions; like
  // PlayerLocomotion, measure the angle from desired back to current before
  // applying it to the current vector.
  static Vector3 AllowedTarget(const PlayerKinematicState &state,
                               const PlayerBodyFacingInput &input,
                               const PlayerBodyFacingParameters &parameters) {
    DO_VALIDATION;
    const Vector3 locomotionFacing =
        state.facing.Get2D().GetNormalized(Vector3(0, -1, 0));
    const Vector3 desired =
        input.desiredFacing.Get2D().GetNormalized(locomotionFacing);
    const float requestedRelative = desired.GetAngle2D(locomotionFacing);
    const float allowedRelative = clamp(requestedRelative,
                                        -parameters.maxRelativeAngle,
                                        parameters.maxRelativeAngle);
    return locomotionFacing.GetRotated2D(allowedRelative).GetNormalized(
        locomotionFacing);
  }

  // Advance one planar torso-orientation step. Turn rate is a hard state
  // invariant. maxRelativeAngle instead bounds the requested target, not
  // necessarily the current body state: a locomotion-facing change can move
  // the cone past a finite-rate torso in one tick, and snapping would violate
  // the continuity contract. The resulting transient overshoot is observable
  // by callers but is resolved continuously toward the bounded target.
  static void Step(PlayerKinematicState &state,
                   const PlayerBodyFacingInput &input,
                   const PlayerBodyFacingParameters &parameters, float dt) {
    DO_VALIDATION;
    assert(dt > 0.0f);
    assert(parameters.maxTurnRate >= 0.0f);
    assert(parameters.maxRelativeAngle >= 0.0f);
    assert(parameters.maxRelativeAngle <= pi);

    const Vector3 locomotionFacing =
        state.facing.Get2D().GetNormalized(Vector3(0, -1, 0));
    const Vector3 current =
        state.bodyFacing.Get2D().GetNormalized(locomotionFacing);
    const Vector3 target = AllowedTarget(state, input, parameters);
    const float requestedTurn = target.GetAngle2D(current);
    const float appliedTurn = clamp(requestedTurn, -parameters.maxTurnRate * dt,
                                    parameters.maxTurnRate * dt);
    state.bodyFacing = current.GetRotated2D(appliedTurn).GetNormalized(current);
    state.bodyFacing.coords[2] = 0.0f;
  }
};

#endif

//
//  legacy_locomotion_command.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_LEGACY_LOCOMOTION_COMMAND
#define _HPP_LEGACY_LOCOMOTION_COMMAND

#include "../../defines.hpp"
#include "humanoid/animcollection.hpp"
#include "core/physics/movement/player/player_movement.hpp"

// Compatibility adapter between the legacy PlayerCommand and the simulation
// -owned locomotion model.
//
// This is deliberately NOT part of PlayerLocomotion. PlayerLocomotion is the
// new simulation physics; this adapter carries the semantics of the old
// animation-driven controller, which the AI still speaks. Keeping them apart
// means the deadband below can simply be deleted once PlayerIntent replaces
// PlayerCommand.
//
// The one piece of legacy semantics that matters is the idle deadband. The
// legacy controller fed the continuous desired speed through
// FloatToEnumVelocity() for animation selection, and anything below the idle
// switch selected an idle animation whose root motion is essentially zero. So a
// small commanded speed meant "stand still" in practice, and the closed loop
// depended on that. Note that the other classes stay continuous: a command of
// 1.8 or 4.5 m/s is not snapped to dribble (3.5) or walk (5.0); only the idle
// class becomes a real deadband.
inline PlayerLocomotionInput BuildLegacyLocomotionInput(
    const PlayerCommand &command, const PlayerKinematicState &state,
    float maxSpeed, const Vector3 &idle_facing_fallback) {
  DO_VALIDATION;
  PlayerLocomotionInput input;

  float desiredSpeed = clamp(command.desiredVelocityFloat, 0.0f, maxSpeed);
  if (FloatToEnumVelocity(desiredSpeed) == e_Velocity_Idle) {
    desiredSpeed = 0.0f;
  }

  input.desiredVelocity =
      command.desiredDirection.Get2D().GetNormalized(state.movementFacing) *
      desiredSpeed;
  // desiredLookAt is a torso/look target, not a locomotion heading, so it only
  // supplies the facing to hold while standing still.
  input.idleFacing =
      command.useDesiredLookAt
          ? (command.desiredLookAt - state.position)
                .Get2D()
                .GetNormalized(state.movementFacing)
          : idle_facing_fallback;
  return input;
}

#endif

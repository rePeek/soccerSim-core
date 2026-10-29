// ANIM-1 A9.4: the legacy spatial (movement/orientation) compatibility state.
//
// `SpatialState` used to be declared in HumanoidBase's header. It is now
// Player-owned compatibility data: the authoritative motion lives in
// PlayerKinematicState, and this struct is a projection of it for the
// not-yet-migrated animation and touch code.
//
// Field attribution after the R4 audit (producer -> consumer / authority):
//   position/angle/directionVec   reset, legacy action root motion and the
//       locomotion projection -> touch, animation selection, prediction.
//       Authority is PlayerKinematicState at every synced point; the legacy
//       action path still writes these before the Player commits the result.
//   movement                      reset, both motion paths -> all of the above.
//       Authority is PlayerKinematicState; this is its animation projection.
//   floatVelocity/enumVelocity    written by the reset, the projection and the
//       action body state -> animation selection, trip commands, diagnostics.
//       They are NOT derived from `movement`: ActionBodyState may clamp the
//       velocity independently when its prefer-velocity policy is disabled, so
//       deriving them would silently change a non-default animation branch.
//   actionSmuggleMovement/        produced by ApplyPostActionTick and the
//   movementSmuggleMovement/      positional correction -> animation motion
//   positionOffsetMovement        input. These are action-correction facts,
//       not motion: they stay in execution until the animation matcher takes
//       them as explicit input.
//   bodyAngle/bodyDirectionVec    reset, action body state and the locomotion
//   relBodyAngle(Nonquantized)/   projection -> animation matching. bodyDirection
//   relBodyDirectionVec           Vec is the continuous torso projection; the
//       relative angles are its animation quantization.
//   foot                          reset and the action clip -> animation query.
//
// No field is a provably safe deletion this round: every one still has a live
// producer and consumer on the non-pure-motion path. Removing the aliases
// requires migrating ComputeAndCommitActionMotion and the touch math first.
#ifndef FOOTBALL_SIM_PLAYER_SPATIAL_STATE_HPP
#define FOOTBALL_SIM_PLAYER_SPATIAL_STATE_HPP

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

using namespace football::math;

struct SpatialState {
  Vector3 position;
  radian angle;
  Vector3 directionVec; // for efficiency, vector version of angle
  e_Velocity enumVelocity;
  float floatVelocity = 0.0f; // for efficiency, float version

  Vector3 movement; // authority: PlayerKinematicState (projected)
  Vector3 actionSmuggleMovement;
  Vector3 movementSmuggleMovement;
  Vector3 positionOffsetMovement;

  radian bodyAngle;
  Vector3 bodyDirectionVec; // for efficiency, vector version of bodyAngle
  radian relBodyAngleNonquantized;
  radian relBodyAngle;
  Vector3 relBodyDirectionVec; // for efficiency, vector version of relBodyAngle
  e_Foot foot;

};

#endif  // FOOTBALL_SIM_PLAYER_SPATIAL_STATE_HPP

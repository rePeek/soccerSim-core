// ANIM-1 A9.4: the legacy spatial (movement/orientation) compatibility state.
//
// `SpatialState` used to be declared in HumanoidBase's header. It is now
// Player-owned compatibility data: the authoritative motion lives in
// PlayerKinematicState, and this struct is a projection of it for the
// not-yet-migrated animation and touch code.
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

  // ANIM-2 A10.6: actualMovement/physicsMovement/animMovement were write-only
  // after the A9 flatten, so they are gone. `movement` remains one of the
  // producers below.
  Vector3 movement; // one of the producers above (default)
  Vector3 actionSmuggleMovement;
  Vector3 movementSmuggleMovement;
  Vector3 positionOffsetMovement;

  radian bodyAngle;
  Vector3 bodyDirectionVec; // for efficiency, vector version of bodyAngle
  radian relBodyAngleNonquantized;
  radian relBodyAngle;
  Vector3 relBodyDirectionVec; // for efficiency, vector version of relBodyAngle
  e_Foot foot;

  void Mirror() {
    position.Mirror();
    movement.Mirror();
    actionSmuggleMovement.Mirror();
    movementSmuggleMovement.Mirror();
    positionOffsetMovement.Mirror();
  }
};

#endif  // FOOTBALL_SIM_PLAYER_SPATIAL_STATE_HPP

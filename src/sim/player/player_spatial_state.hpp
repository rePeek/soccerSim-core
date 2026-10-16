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

  Vector3 actualMovement;
  Vector3 physicsMovement; // ignores effects like positionoffset
  Vector3 animMovement;
  Vector3 movement; // one of the above (default)
  Vector3 actionSmuggleMovement;
  Vector3 movementSmuggleMovement;
  Vector3 positionOffsetMovement;

  radian bodyAngle;
  Vector3 bodyDirectionVec; // for efficiency, vector version of bodyAngle
  radian relBodyAngleNonquantized;
  radian relBodyAngle;
  Vector3 relBodyDirectionVec; // for efficiency, vector version of relBodyAngle
  Vector3 relBodyDirectionVecNonquantized;
  e_Foot foot;

  void Mirror() {
    position.Mirror();
    actualMovement.Mirror();
    physicsMovement.Mirror();
    animMovement.Mirror();
    movement.Mirror();
    actionSmuggleMovement.Mirror();
    movementSmuggleMovement.Mirror();
    positionOffsetMovement.Mirror();
  }
};

#endif  // FOOTBALL_SIM_PLAYER_SPATIAL_STATE_HPP

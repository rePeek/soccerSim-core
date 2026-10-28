//
//  player_ground_collider.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_GROUND_COLLIDER
#define _HPP_PLAYER_GROUND_COLLIDER

#include "football/foundation/collision/penetration2d.hpp"
#include "football/foundation/math/vector3.hpp"

using namespace football::math;


// A presentation-independent horizontal body volume for ordinary player
// contests. Action-specific volumes (slides, saves, and touches) remain in
// PlayerActionState and will be layered on top of this base collider.
struct PlayerGroundCollider {
  Vector3 center = Vector3(0);
  float radius = 0.36f;

  void SetCenter(const Vector3 &position) { center = position.Get2D(); }

  // Geometry lives in football::collision; this keeps the legacy strict
  // tangency semantics (touching is not a contest).
  bool Intersects(const PlayerGroundCollider &other) const {
    return football::collision::PenetratesCircleCircle2D(center, radius,
                                                         other.center,
                                                         other.radius);
  }


};

#endif

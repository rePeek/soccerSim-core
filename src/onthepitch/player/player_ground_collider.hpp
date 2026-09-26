//
//  player_ground_collider.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_GROUND_COLLIDER
#define _HPP_PLAYER_GROUND_COLLIDER

#include "../../defines.hpp"

// A presentation-independent horizontal body volume for ordinary player
// contests. Action-specific volumes (slides, saves, and touches) remain in
// PlayerActionState and will be layered on top of this base collider.
struct PlayerGroundCollider {
  Vector3 center = Vector3(0);
  float radius = 0.36f;

  void SetCenter(const Vector3 &position) { center = position.Get2D(); }

  bool Intersects(const PlayerGroundCollider &other) const {
    return (center - other.center).Get2D().GetLength() < radius + other.radius;
  }

  void Mirror() { center.Mirror(); }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(center);
    state->process(radius);
  }
};

#endif

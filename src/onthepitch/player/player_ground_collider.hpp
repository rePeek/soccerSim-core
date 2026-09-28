//
//  player_ground_collider.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_GROUND_COLLIDER
#define _HPP_PLAYER_GROUND_COLLIDER

#include "core/domain/player/player_profile.hpp"
#include "core/state/player_state.hpp"
#include "../../defines.hpp"

// A presentation-independent horizontal body volume for ordinary player
// contests. Action-specific volumes (slides, saves, and touches) remain in
// PlayerActionState and will be layered on top of this base collider.
//
// This is a Derived value: Profile + State -> Collider. It owns no authority.
// PlayerBase keeps one copy only as a serialized compatibility shadow; that
// copy is not a second source of truth and save-format removal is deferred.
struct PlayerGroundCollider {
  Vector3 center = Vector3(0);
  float radius = 0.36f;

  void SetCenter(const Vector3 &position) { center = position.Get2D(); }

  bool Intersects(const PlayerGroundCollider &other) const {
    return (center - other.center).Get2D().GetLength() < radius + other.radius;
  }

  void Mirror() { center.Mirror(); }

  // Serialized compatibility projection. center and radius are derived from
  // PlayerState/PlayerProfile, so this stream is redundant data kept only to
  // preserve the legacy save format; removing it is deferred.
  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(center);
    state->process(radius);
  }
};

// Canonical construction. Radius is an inherent property of the player, the
// center is current kinematics; neither is stored authoritatively anywhere else.
inline PlayerGroundCollider BuildPlayerGroundCollider(
    const football::domain::PlayerProfile &profile,
    const PlayerState &state) {
  PlayerGroundCollider collider;
  collider.center = state.position.Get2D();
  collider.radius = profile.physical.bodyRadius;
  return collider;
}

#endif

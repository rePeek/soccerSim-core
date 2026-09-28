//
//  player_action_volume.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_ACTION_VOLUME
#define _HPP_PLAYER_ACTION_VOLUME

#include "player_action.hpp"
#include "core/contact/player_collider.hpp"
#include "core/physics/player_movement.hpp"

// Contact volume for actions that reach out beyond the base body collider
// (slide tackles, standing tackles). It is derived purely from simulation
// state -- PlayerActionState says which action and which phase, and
// PlayerKinematicState says where the actor is and which way it faces -- so no
// scene object or animation pose is involved.
//
// Shape: a horizontal capsule from the actor's position extending forward
// along `axis`. Reach and radius are per action type; this is a first-order
// approximation, deliberately not a body-mesh collision.
struct PlayerActionVolume {
  bool active = false;
  Vector3 origin = Vector3(0);
  Vector3 axis = Vector3(0, -1, 0);
  float length = 0.0f;
  float radius = 0.0f;

  // Capsule [origin, origin + axis * length] against a horizontal circle.
  // Everything is evaluated on the pitch plane, so body height is ignored.
  bool Intersects(const football::contact::CircleCollider &other) const {
    if (!active) return false;
    const Vector3 toCenter = (other.center - origin).Get2D();
    const float along = clamp(toCenter.GetDotProduct(axis), 0.0f, length);
    const Vector3 closest = origin + axis * along;
    return (other.center - closest).Get2D().GetLength() < radius + other.radius;
  }
};

struct PlayerActionVolumeParameters {
  // Action phase window in which the reach volume exists at all.
  int contactWindowStartFrame = 5;
  int contactWindowEndFrame = 28;

  // Slide tackle: long forward reach, narrow.
  float slideReach = 1.6f;
  float slideRadius = 0.45f;

  // Standing tackle (interfere): shorter and broader.
  float interfereReach = 0.9f;
  float interfereRadius = 0.55f;
};

// Returns an inactive volume for actions that do not reach out (movement,
// trips, passes, shots, ...).
inline PlayerActionVolume BuildTackleVolume(
    const PlayerActionState &action, const PlayerKinematicState &kinematics,
    const PlayerActionVolumeParameters &parameters = PlayerActionVolumeParameters()) {
  DO_VALIDATION;

  PlayerActionVolume volume;
  if (action.type != e_FunctionType_Sliding &&
      action.type != e_FunctionType_Interfere) {
    return volume;
  }
  if (action.frame <= parameters.contactWindowStartFrame ||
      action.frame >= parameters.contactWindowEndFrame) {
    return volume;
  }

  volume.active = true;
  volume.origin = kinematics.position.Get2D();
  // The facing vector is not mirrored by the legacy spatial-state mirror, so it
  // is used as-is here; see PlayerKinematicState::Mirror().
  volume.axis = kinematics.movementFacing.Get2D().GetNormalized(Vector3(0, -1, 0));
  if (action.type == e_FunctionType_Sliding) {
    volume.length = parameters.slideReach;
    volume.radius = parameters.slideRadius;
  } else {
    volume.length = parameters.interfereReach;
    volume.radius = parameters.interfereRadius;
  }
  return volume;
}

#endif

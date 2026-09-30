//
//  player_body_collider.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_BODY_COLLIDER
#define _HPP_PLAYER_BODY_COLLIDER

#include <array>

#include "sim/player/player_kinematics.hpp"

// Approximate body volume used for *passive* ball-body collisions.
//
// Keep the three collider concepts apart:
//
//   PlayerGroundCollider  player-player ordinary contest (contact radius)
//   PlayerBodyCollider    ball hitting a body (body geometry)
//   PlayerActionVolume    tackle / save / reach actions (active contact)
//
// This is deliberately neither the render geometry (per-part AABBs of the
// posed skeleton) nor PlayerGroundCollider. PlayerGroundCollider.radius is a
// *contact* radius (two players touch at 0.36 + 0.36 = 0.72 m), while the body
// is only about 0.23 m half wide. Reusing the contact radius here would double
// the ball's contact distance.
//
// Sizes were calibrated from measured rest-pose part extents (see step.md),
// but they are simulation-owned constants: nothing here comes from a scene node
// or an animation pose. The primitives are upright, so the collider depends
// only on the player's position -- not on facing -- which also means it has no
// mirroring semantics to get wrong.
struct BodyVolume {
  Vector3 center = Vector3(0);
  float radius = 0.0f;
  float halfHeight = 0.0f;  // vertical half length; 0 means sphere

  // Vertical capsule (or sphere when halfHeight == 0) against a sphere.
  bool IntersectsSphere(const Vector3 &sphereCenter, float sphereRadius) const {
    const float clampedZ =
        clamp(sphereCenter.coords[2] - center.coords[2], -halfHeight, halfHeight);
    const Vector3 closest(center.coords[0], center.coords[1],
                          center.coords[2] + clampedZ);
    return (sphereCenter - closest).GetLength() < radius + sphereRadius;
  }
};

struct PlayerBodyColliderParameters {
  // Torso. Measured trunk half extents (0.229, 0.124, 0.304) around z = 1.11,
  // pelvis (0.196, 0.120, 0.147) around z = 0.96.
  float upperBodyRadius = 0.22f;
  float upperBodyCenterZ = 1.11f;
  float upperBodyHalfHeight = 0.30f;

  // Lower body. Upper legs sit at |x| = 0.087 with half width 0.102, lower
  // legs around z = 0.53, feet around z = 0.09.
  float lowerBodyRadius = 0.19f;
  float lowerBodyCenterZ = 0.60f;
  float lowerBodyHalfHeight = 0.55f;

  // Head. Measured half extents (0.089, 0.118, 0.166) around z = 1.61.
  float headRadius = 0.11f;
  float headCenterZ = 1.61f;
};

struct PlayerBodyCollider {
  BodyVolume upperBody;
  BodyVolume lowerBody;
  BodyVolume head;

  std::array<const BodyVolume *, 3> GetVolumes() const {
    return {&upperBody, &lowerBody, &head};
  }
};

inline PlayerBodyCollider BuildBodyCollider(
    const PlayerKinematicState &kinematics,
    const PlayerBodyColliderParameters &parameters =
        PlayerBodyColliderParameters()) {
  DO_VALIDATION;

  const Vector3 origin = kinematics.position.Get2D();

  PlayerBodyCollider collider;

  collider.upperBody.center =
      origin + Vector3(0.0f, 0.0f, parameters.upperBodyCenterZ);
  collider.upperBody.radius = parameters.upperBodyRadius;
  collider.upperBody.halfHeight = parameters.upperBodyHalfHeight;

  collider.lowerBody.center =
      origin + Vector3(0.0f, 0.0f, parameters.lowerBodyCenterZ);
  collider.lowerBody.radius = parameters.lowerBodyRadius;
  collider.lowerBody.halfHeight = parameters.lowerBodyHalfHeight;

  collider.head.center = origin + Vector3(0.0f, 0.0f, parameters.headCenterZ);
  collider.head.radius = parameters.headRadius;
  collider.head.halfHeight = 0.0f;

  return collider;
}

#endif

#ifndef _HPP_CORE_CONTACT_PLAYER_BODY_COLLIDER
#define _HPP_CORE_CONTACT_PLAYER_BODY_COLLIDER

#include "core/physics/contact/geometry/capsule_collider.hpp"

namespace football_sim::contact {

// Approximate passive body volume: three upright capsules (upper torso,
// lower body, head as a degenerate capsule). Geometry only -- built from a
// ground position -- with no dependency on PlayerBase, Humanoid or PlayerData.
//
// Sizes were calibrated from measured rest-pose part extents; see the legacy
// player_body_collider.hpp. They are the *body* extents, not the
// CircleCollider contact radius (players touch at 0.36 + 0.36 = 0.72 m, but
// the body is only about half that wide).
struct PlayerBodyColliderParameters {
  float upperRadius = 0.22f;
  float upperCenterZ = 1.11f;
  float upperHalfHeight = 0.30f;

  float lowerRadius = 0.19f;
  float lowerCenterZ = 0.60f;
  float lowerHalfHeight = 0.55f;

  float headRadius = 0.11f;
  float headCenterZ = 1.61f;
};

struct PlayerBodyCollider {
  CapsuleCollider upperBody;
  CapsuleCollider lowerBody;
  CapsuleCollider head;
};

inline CapsuleCollider MakeVerticalCapsule(const football_sim::math::Vector3 &origin,
                                           float radius, float centerZ,
                                           float halfHeight) {
  CapsuleCollider capsule;
  capsule.tipA = origin + football_sim::math::Vector3(0.0f, 0.0f, centerZ - halfHeight);
  capsule.tipB = origin + football_sim::math::Vector3(0.0f, 0.0f, centerZ + halfHeight);
  capsule.radius = radius;
  return capsule;
}

inline PlayerBodyCollider BuildPlayerBodyCollider(
    const football_sim::math::Vector3 &position,
    const PlayerBodyColliderParameters &parameters =
        PlayerBodyColliderParameters()) {
  const football_sim::math::Vector3 origin = position.Get2D();
  PlayerBodyCollider body;
  body.upperBody = MakeVerticalCapsule(origin, parameters.upperRadius,
                                       parameters.upperCenterZ,
                                       parameters.upperHalfHeight);
  body.lowerBody = MakeVerticalCapsule(origin, parameters.lowerRadius,
                                       parameters.lowerCenterZ,
                                       parameters.lowerHalfHeight);
  body.head = MakeVerticalCapsule(origin, parameters.headRadius,
                                  parameters.headCenterZ, 0.0f);
  return body;
}

}  // namespace football_sim::contact

#endif  // _HPP_CORE_CONTACT_PLAYER_BODY_COLLIDER
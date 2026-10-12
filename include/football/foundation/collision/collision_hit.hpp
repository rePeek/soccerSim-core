#ifndef FOOTBALL_COLLISION_COLLISION_HIT_HPP
#define FOOTBALL_COLLISION_COLLISION_HIT_HPP

// Query result and query input for football::collision.
//
// collision answers "did they touch, when, and from which direction". It never
// answers "how do they move afterwards": that (impulse, restitution, friction)
// belongs to ball/player.

#include "football/foundation/geometry/collider.hpp"
#include "football/foundation/geometry/shapes.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::collision {

// Kinematic input to one collision query: a moving sphere. Not a Ball -- it
// carries no mass, spin, material or match identity, only the geometry and the
// velocity needed to sweep it.
struct MovingSphere {
  football::geometry::Sphere shape;
  football::math::Vector3 velocity;
};

// Result of one continuous collision query.
//   toi               first contact time as a fraction of the swept interval [0, 1]
//   center            sphere center at TOI
//   normal            unit normal from the collider surface toward the sphere
//   surface_velocity  collider contact-surface velocity at TOI (zero if static)
// Response (normal impulse, restitution, friction) is deliberately absent.
struct CollisionHit {
  football::geometry::ColliderId collider = 0;
  float toi = 0.0f;
  football::math::Vector3 center;
  football::math::Vector3 normal;
  football::math::Vector3 surface_velocity;
};

}  // namespace football::collision

#endif  // FOOTBALL_COLLISION_COLLISION_HIT_HPP

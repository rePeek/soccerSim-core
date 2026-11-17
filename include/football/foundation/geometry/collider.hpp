#pragma once

// Collision-geometry contract: shapes tagged for deterministic ordering, plus
// their per-tick motion and contact material. Owned by no single domain: ball,
// pitch and the player body all produce or consume these values. No ball/rules
// semantics live here.
//
// ContactMaterial deliberately lives in geometry for now: the producers (pitch,
// player body) already attach it to the collider they emit, and collision never
// reads it, so a separate material/physics module would add a hop without a
// consumer. Revisit once the player body shares the generic collision path and
// the right home for shared contact materials is clear.

#include <cstdint>
#include <variant>

#include "football/foundation/geometry/shapes.hpp"

namespace football::geometry {

using ColliderId = std::uint32_t;

using ColliderShape = std::variant<Plane, HalfSpace, Sphere, Capsule>;

struct ContactMaterial {
  float restitution = 0.5f;
  float friction = 0.0f;
  bool operator==(const ContactMaterial&) const = default;
};

// Static collider: one pose plus material. Scene-owned geometry (the pitch today,
// arena furniture later) uses this. Ids stay stable for deterministic ordering
// and carry no Player/Team/rules identity.
struct Collider {
  ColliderId id = 0;
  ColliderShape shape;
  ContactMaterial material;
  bool operator==(const Collider&) const = default;
};


}  // namespace football::geometry

#pragma once

#include <string_view>
#include <variant>

#include "football/foundation/geometry/shapes.hpp"

namespace football::geometry {

using ColliderShape = std::variant<Plane, HalfSpace, Sphere, Capsule>;

struct ContactMaterial {
  float restitution = 0.5f;
  float friction = 0.0f;
  bool operator==(const ContactMaterial&) const = default;
};

// One collider: what it looks like, how it responds, and a label for logs.
// `name` is not an identity -- it may be empty or repeated, never orders contacts
// and never keys a lookup. Who a collider belongs to (a body, a side, a rule)
// is knowledge the owner keeps, not something geometry carries.
struct Collider {
  ColliderShape shape;
  ContactMaterial material;
  std::string_view name = {};
  bool operator==(const Collider&) const = default;
};

}  // namespace football::geometry

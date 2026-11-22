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

struct Collider {
  ColliderShape shape;
  ContactMaterial material;
  std::string_view name = {};
  bool operator==(const Collider&) const = default;
};

}

#pragma once


// Shared shape primitives. geometry owns what things look like; collision
// (football/foundation/collision) owns whether two shapes touch. No physics
// (mass, friction, response) lives here.

#include "football/foundation/math/vector3.hpp"

namespace football::geometry {

// An infinite, two-sided geometric plane: a surface with no thickness and no
// solid side. Overlap/CCD against it treat a sphere as touching when its
// centre is within one radius of the surface, on either side.
struct Plane {
  math::Vector3 point;
  math::Vector3 normal;
  bool operator==(const Plane&) const = default;
};

// A solid half-space: the region on the -normal side of the surface is solid.
// The valid (non-penetrating) side is (p - point)*normal >= 0, so the ground
// is HalfSpace{{0,0,0},{0,0,1}} and bodies must stay above z = 0.
struct HalfSpace {
  math::Vector3 point;
  math::Vector3 normal;
  bool operator==(const HalfSpace&) const = default;
};

struct Sphere {
  math::Vector3 center;
  float radius = 0.0f;
  bool operator==(const Sphere&) const = default;
};

struct Capsule {
  math::Vector3 a;
  math::Vector3 b;
  float radius = 0.0f;
  bool operator==(const Capsule&) const = default;
};

}  // namespace football::geometry

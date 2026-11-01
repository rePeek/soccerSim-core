#pragma once

#include <array>
#include <stdexcept>

#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// The declared 14-bone model: hierarchy and bind offsets.
//
// This is the skeleton contract, not a foot-support policy. It is shared by the
// Player's forward kinematics, the colliders and the offline/animation
// verification, so a bind change cannot land in only one of them. The support
// derivation in `foot_support.hpp` consumes it; production pose evaluation
// includes only this header.
struct SkeletonBindJoint {
  BodyPart part;
  BodyPart parent;
  football::math::Vector3 offset;
};

inline const std::array<SkeletonBindJoint, 13> kPlayerBindJoints{{
    {body, player, {0, 0, .96f}},
    {middle, body, {0, 0, .15f}},
    {neck, middle, {0, -.03f, .5f}},
    {left_thigh, body, {.087f, 0, -.01f}},
    {right_thigh, body, {-.087f, 0, -.01f}},
    {left_knee, left_thigh, {0, 0, -.42f}},
    {right_knee, right_thigh, {0, 0, -.42f}},
    {left_ankle, left_knee, {0, -.04f, -.44f}},
    {right_ankle, right_knee, {0, -.04f, -.44f}},
    {left_shoulder, middle, {.16f, -.01f, .48f}},
    {right_shoulder, middle, {-.16f, -.01f, .48f}},
    {left_elbow, left_shoulder, {-.01f, 0, -.33f}},
    {right_elbow, right_shoulder, {.01f, 0, -.33f}},
}};

inline const football::math::Vector3& PlayerBindOffset(BodyPart part) {
  for (const auto& bind : kPlayerBindJoints) {
    if (bind.part == part) return bind.offset;
  }
  throw std::out_of_range("no bind offset for this body part");
}

inline BodyPart PlayerBindParent(BodyPart part) {
  for (const auto& bind : kPlayerBindJoints) {
    if (bind.part == part) return bind.parent;
  }
  // The root has no parent bind; it anchors the hierarchy.
  return part;
}

#ifndef FOOTBALL_ANIMATION_FOOT_SUPPORT_HPP
#define FOOTBALL_ANIMATION_FOOT_SUPPORT_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// Foot support phases, as an animation-domain fact.
//
// A gait clip knows which foot is carrying weight and when. Player Execution
// needs that answer to decide when a world-space support anchor may be planted
// or released; deriving it from ankle height alone mislabels authored clips (a
// swinging foot can hang lower than a stance foot), and deriving it from the
// settled pose cannot work at all (a locked foot keeps its height).
//
// The phases are derived from the clip's own pose track at load time
// (`DeriveFootSupport`), not stored in the artifact: one implementation, no
// Python/C++ duplication, and a bake-time track can replace it later without
// changing a single caller.

// The declared 14-bone model: the hierarchy and its bind offsets. Shared by the
// animation support derivation and the Player's forward kinematics, colliders
// and foot solve, so a bind change cannot land in only one of them.
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

// Metres above the clip's lowest ankle at which a foot still counts as down,
// and the level at which it is considered lifted again (hysteresis).
inline constexpr float kFootDownHeight = 0.04f;
inline constexpr float kFootLiftHeight = 0.09f;

// Derives the per-frame support track from the clip's own leg poses.
//
// `clip.foot_support` and `clip.stance_speed` are filled by AnimationLibrary on
// load. `stance_speed` is how fast a stance foot travels backwards inside the
// clip, in metres per second: it is the speed the *foot* implies, which for
// authored clips differs from the root track (the animation has foot slide baked
// in). Playback prefers it, so the plant does not have to absorb the difference.
void DeriveFootSupport(AnimationClip& clip);

// One support frame at a fractional clip time. Nearest baked frame, clamped at
// both ends; a clip without a derived track reports both feet swinging, which
// tells the caller to fall back to its own detection.
FootSupportFrame SampleFootSupport(const AnimationClip& clip, float frame_time);

// RMA-1: measures whether the clip's own root track and support foot agree
// enough for the IK-free adaptation. Runs after DeriveFootSupport and fills
// `clip.motion_quality`; it never modifies the clip's motion or poses. A clip
// that fails the gate keeps the compatibility path - the flags are a
// diagnostic of the source animation, not a clamp.
void DeriveMotionQuality(AnimationClip& clip);

#endif  // FOOTBALL_ANIMATION_FOOT_SUPPORT_HPP

//
//  player_retain_anchor.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_RETAIN_ANCHOR
#define _HPP_PLAYER_RETAIN_ANCHOR

#include "../../defines.hpp"

// Body-local anchor that a retained ball sticks to.
//
// This replaces reading a body-part transform out of the skeletal animation
// tree:
//
//   retain state string
//     -> BodyPartFromString
//     -> nodeMap[bodyPart]
//     -> GetDerivedPosition() / GetDerivedRotation()
//     -> Ball::SetPosition()
//
// That chain made the retained ball's simulation position depend on the
// animation pose, and therefore on when the presentation pipeline last
// refreshed it. The anchor below is a pure function of simulation state.
//
// Audited values in the animation corpus: only "right_elbow" occurs (20
// incoming / 29 outgoing occurrences). Animation's variable cache mirror()
// flips a leading left/right, so "left_elbow" is reachable at runtime too.
enum class RetainAnchorKind {
  RightElbow,
  LeftElbow,
};

// Returns false for unknown state strings. Callers must treat that as an
// error: the legacy code had `assert(bodyPart)`, and a silent fallback would
// put the ball somewhere plausible but wrong, which is very hard to spot.
inline bool ParseRetainAnchorKind(const std::string &state,
                                  RetainAnchorKind &kind) {
  DO_VALIDATION;
  if (state == "right_elbow") {
    kind = RetainAnchorKind::RightElbow;
    return true;
  }
  if (state == "left_elbow") {
    kind = RetainAnchorKind::LeftElbow;
    return true;
  }
  return false;
}

// Anchor offsets expressed in the body frame, in metres.
//
// Calibration basis: the measured rest-pose lower-arm centres (right arm at
// |x| = 0.150, z = 1.26; the left arm is its mirror). These are body-semantic
// offsets, deliberately not a reproduced skeleton wrist position.
struct RetainAnchorOffset {
  float forward = 0.0f;
  float right = 0.0f;
  float height = 0.0f;
};

inline RetainAnchorOffset GetRetainAnchorOffset(RetainAnchorKind kind) {
  DO_VALIDATION;
  RetainAnchorOffset offset;
  offset.height = 1.26f;
  offset.right = kind == RetainAnchorKind::RightElbow ? 0.15f : -0.15f;
  return offset;
}

// Pure function of simulation state: no Node, no Geometry, no animation pose.
// The engine's own convention calls GetRotated2D(+angle) the left side, so the
// right side is the negative rotation.
inline Vector3 ComputeRetainAnchor(const Vector3 &position,
                                   const Vector3 &bodyDirection,
                                   RetainAnchorKind kind) {
  DO_VALIDATION;
  const RetainAnchorOffset local = GetRetainAnchorOffset(kind);
  const Vector3 forward =
      bodyDirection.Get2D().GetNormalized(Vector3(0, -1, 0));
  const Vector3 right = forward.GetRotated2D(-0.5f * pi);
  return position + forward * local.forward + right * local.right +
         Vector3(0, 0, local.height);
}

#endif

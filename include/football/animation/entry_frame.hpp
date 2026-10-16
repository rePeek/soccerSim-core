#ifndef FOOTBALL_ANIMATION_ENTRY_FRAME_HPP
#define FOOTBALL_ANIMATION_ENTRY_FRAME_HPP

#include "football/animation/clip.hpp"

// ANIM-2 A13 — pose-based entry-frame matching.
//
// The legacy install always began a clip at frame 0. Arbitrary-frame matching
// chooses a legal entry frame whose pose best matches the actor's current final
// pose, so a clip switch (or a collision reaction) starts from the state the
// actor is actually in instead of snapping back to the first keyframe.
//
// Pure: reads only the immutable clip and the supplied pose. No Player, Ball,
// Simulation or RNG. Animation still owns only selection; the Player still owns
// the action clock, the world motion and the install.
//
// ANIM-2 A13.0: the body lives in src/animation/entry_frame.cpp.

namespace football::animation {

// A symmetric pose distance over every joint: summed shortest-path rotation
// difference plus the summed joint position difference. Smaller is closer; two
// identical poses give exactly 0.
float PoseDistance(const PoseFrame& a, const PoseFrame& b);

struct EntryFrameRequest {
  // The actor's current final pose. A null pose means "no match is possible",
  // and the caller keeps its default (frame 0).
  const PoseFrame* reference_pose = nullptr;
  // How many frames must remain after the entry frame. A clip must not be
  // entered so late that the action cannot complete.
  int min_remaining_frames = 1;
  // Optional hard upper bound on the entry frame (inclusive). -1 derives it as
  // `frame_count - 1 - min_remaining_frames`.
  int last_legal_frame = -1;
};

struct EntryFrameResult {
  // The chosen entry frame. 0 when no frame was selected (matched == false).
  int frame = 0;
  float pose_distance = 0.0f;
  // True when a reference pose was supplied and at least one legal frame was
  // evaluated. When false the caller must keep frame 0.
  bool matched = false;
};

EntryFrameResult SelectEntryFrame(const AnimationClip& clip,
                                  const EntryFrameRequest& request);

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_ENTRY_FRAME_HPP

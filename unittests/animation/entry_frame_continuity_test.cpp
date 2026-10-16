// ANIM-2 A15.0 — entry-frame continuity: a matched entry frame must blend in
// with a smaller first-tick jump than a naive frame-0 restart.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "animation_test_support.hpp"
#include "football/animation/entry_frame.hpp"
#include "football/animation/transition.hpp"

using namespace football::animation;

namespace {

AnimationClip PosesClip(uint32_t id, int frames) {
  AnimationClip clip;
  clip.id = id;
  clip.frame_count = static_cast<uint32_t>(frames);
  clip.poses.resize(static_cast<std::size_t>(frames));
  for (int f = 0; f < frames; ++f) {
    clip.poses[static_cast<std::size_t>(f)] =
        PoseWith(0.1f * static_cast<float>(f), 12.0f * static_cast<float>(f));
  }
  return clip;
}

float MaxJointJump(const PoseFrame& a, const PoseFrame& b) {
  float jump = 0.0f;
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    jump = std::max(
        jump, static_cast<float>(a.orientations[j].GetRotationAngle(b.orientations[j])));
  }
  return jump;
}

}  // namespace

TEST_CASE("a matched entry frame reduces the first-tick pose jump") {
  const AnimationClip clip = PosesClip(1, 20);
  const PoseFrame current = clip.poses[12];

  EntryFrameRequest request;
  request.reference_pose = &current;
  request.min_remaining_frames = 1;
  const EntryFrameResult matched = SelectEntryFrame(clip, request);
  REQUIRE(matched.matched);
  CHECK(matched.frame == 12);

  // Entering at the matched frame blends from a pose that is already close.
  AnimationTransitionState matched_transition;
  matched_transition.Begin(current, 0.0f, kActionBlendTicks);
  const PoseFrame matched_first =
      matched_transition.Advance(clip.poses[matched.frame], 0.0f);

  // Restarting at frame 0 has to travel the whole pose difference in one blend.
  AnimationTransitionState frame0_transition;
  frame0_transition.Begin(current, 0.0f, kActionBlendTicks);
  const PoseFrame frame0_first = frame0_transition.Advance(clip.poses[0], 0.0f);

  const float matched_jump = MaxJointJump(current, matched_first);
  const float frame0_jump = MaxJointJump(current, frame0_first);
  CHECK(matched_jump < frame0_jump);
  CHECK(matched_jump < 0.01f);
}

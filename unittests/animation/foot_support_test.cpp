// Foot support derivation: which foot of a baked clip carries weight, and how
// fast the stance foot travels inside it.
//
// Synthetic clips with a hand-made height pattern (so the expected phases are
// known exactly) plus the shipped catalog, where the derived track has to agree
// with the clip's own gait.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "football/animation/foot_support.hpp"
#include "football/animation/library.hpp"

using football::math::Vector3;

namespace {

// A clip whose two legs alternate between "straight down" and "lifted": the
// lifted leg rotates its thigh forward, which raises the ankle.
AnimationClip AlternatingClip(int frames = 20, int half = 10) {
  AnimationClip clip;
  clip.id = 1;
  clip.frame_count = static_cast<uint32_t>(frames);
  clip.root_positions.assign(frames, Vector3(0, 0, 0));
  for (int f = 0; f < frames; ++f) {
    PoseFrame pose;
    const bool left_up = f >= half;
    pose.orientations[left_up ? BodyPart::left_thigh : BodyPart::right_thigh]
        .SetAngleAxis(0.7f, Vector3(1, 0, 0));
    // A straight leg hangs slightly further forward than a lifted one, which is
    // what the stance-speed derivation reads.
    clip.poses.push_back(pose);
    clip.root_positions[static_cast<std::size_t>(f)] =
        Vector3(0, -0.02f * static_cast<float>(f), 0);
  }
  return clip;
}

}  // namespace

TEST_CASE("the derived track marks landing, stance and takeoff per foot",
          "[animation][footsupport]") {
  AnimationClip clip = AlternatingClip();
  DeriveFootSupport(clip);
  REQUIRE(clip.foot_support.size() == clip.frame_count);

  // First half: the left leg hangs, the right is lifted.
  CHECK(FootIsSupporting(clip.foot_support.front().left));
  CHECK_FALSE(FootIsSupporting(clip.foot_support.front().right));
  CHECK(clip.foot_support.front().left == FootPhase::Landing);
  CHECK(clip.foot_support[9].left == FootPhase::Takeoff);
  CHECK(clip.foot_support[5].left == FootPhase::Stance);

  // Second half: mirrored.
  CHECK(FootIsSupporting(clip.foot_support[10].right));
  CHECK_FALSE(FootIsSupporting(clip.foot_support[10].left));
  CHECK(clip.foot_support[10].right == FootPhase::Landing);
  CHECK(clip.foot_support[19].right == FootPhase::Takeoff);

  // Never both feet in the air for a walking pattern.
  for (const auto& frame : clip.foot_support) {
    CHECK((FootIsSupporting(frame.left) || FootIsSupporting(frame.right)));
  }
}

TEST_CASE("the derivation is deterministic and survives an empty clip",
          "[animation][footsupport]") {
  AnimationClip a = AlternatingClip();
  AnimationClip b = AlternatingClip();
  DeriveFootSupport(a);
  DeriveFootSupport(b);
  REQUIRE(a.foot_support.size() == b.foot_support.size());
  for (std::size_t f = 0; f < a.foot_support.size(); ++f) {
    CHECK(a.foot_support[f].left == b.foot_support[f].left);
    CHECK(a.foot_support[f].right == b.foot_support[f].right);
  }
  CHECK(a.stance_speed == b.stance_speed);

  AnimationClip empty;
  DeriveFootSupport(empty);
  CHECK(empty.foot_support.empty());
  CHECK(empty.stance_speed == Catch::Approx(0.0f));
}

TEST_CASE("sampling a support track is a clamped nearest-frame lookup",
          "[animation][footsupport]") {
  AnimationClip clip = AlternatingClip();
  DeriveFootSupport(clip);
  // Rounds to the nearest baked frame and clamps at both ends.
  CHECK(SampleFootSupport(clip, 0.0f).left == FootPhase::Landing);
  CHECK(SampleFootSupport(clip, 0.4f).left == FootPhase::Landing);
  CHECK(SampleFootSupport(clip, 0.6f).left ==
        SampleFootSupport(clip, 1.0f).left);
  CHECK(SampleFootSupport(clip, -5.0f).left ==
        SampleFootSupport(clip, 0.0f).left);
  CHECK(SampleFootSupport(clip, 1e6f).right ==
        SampleFootSupport(clip, 19.0f).right);
  CHECK(SampleFootSupport(clip, std::nanf("")).left ==
        SampleFootSupport(clip, 0.0f).left);

  // A clip without a track reports "nothing known", which is the caller's signal
  // to fall back to its own detection.
  AnimationClip untracked;
  const auto fallback = SampleFootSupport(untracked, 3.0f);
  CHECK_FALSE(FootIsSupporting(fallback.left));
  CHECK_FALSE(FootIsSupporting(fallback.right));
}

TEST_CASE("the shipped catalog derives a usable support track", "[animation][footsupport]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  int movement_clips = 0;
  int clips_with_stance = 0;
  for (const auto& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    if (clip.frame_count < 4) continue;
    REQUIRE(clip.foot_support.size() == clip.frame_count);
    ++movement_clips;
    int supported = 0;
    for (const auto& frame : clip.foot_support) {
      if (FootIsSupporting(frame.left) || FootIsSupporting(frame.right)) {
        ++supported;
      }
    }
    // A Movement clip that never carries weight would be unusable.
    CHECK(supported > 0);
    if (clip.stance_speed > 0.5f) ++clips_with_stance;
  }
  REQUIRE(movement_clips > 100);
  // Most locomotion clips have a measurable stance phase; idle clips do not.
  CHECK(clips_with_stance > movement_clips / 2);
}

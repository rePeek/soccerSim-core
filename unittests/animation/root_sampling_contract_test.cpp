#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "football/animation/motion_clip.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/reference_sample.hpp"

using football::math::Vector3;

namespace {
AnimationClip Track() {
  AnimationClip clip;
  clip.frame_count = 5;
  // Non-zero origin and non-uniform travel expose origin/interpolation errors.
  clip.root_positions = {{2, 3, 0}, {2.1f, 2.8f, 0}, {2.15f, 2.3f, 0},
                         {2.4f, 2.1f, 0}, {2.5f, 1.7f, 0}};
  clip.poses.resize(clip.frame_count);
  return clip;
}
}

TEST_CASE("clamped root interpolation is origin-relative and pose-independent",
          "[animation][sampling]") {
  auto clip = Track();
  CHECK(SampleRootPosition(clip.root_positions, -4.0f) == Vector3(0));
  CHECK(SampleRootPosition(clip.root_positions, 0.0f) == Vector3(0));
  CHECK(SampleRootPosition(clip.root_positions, 99.0f) ==
        clip.root_positions.back() - clip.root_positions.front());
  const auto expected = clip.root_positions[1] - clip.root_positions[0] +
      (clip.root_positions[2] - clip.root_positions[1]) * 0.5f;
  CHECK(SampleRootPosition(clip.root_positions, 1.5f) == expected);
  CHECK(SampleRootPosition({}, 1.0f) == Vector3(0));
  clip.root_positions.resize(1);
  CHECK(SampleRootPosition(clip.root_positions, 1.0f) == Vector3(0));
}

TEST_CASE("discrete extraction agrees with forward clamped paired sampling within roundoff",
          "[animation][sampling]") {
  const auto clip = Track();
  const auto motion = ExtractMotionClip(clip);
  // MotionClip accumulates per-frame deltas; direct root interpolation does not.
  // Test mathematical equivalence, not bitwise identity of different sums.
  for (float from : {-3.0f, 0.0f, 0.25f, 1.75f, 4.0f, 9.0f}) {
    for (float to : {0.0f, 0.5f, 2.25f, 4.0f, 10.0f}) {
      if (to < from) continue;
      const auto paired = SampleAnimationStep(clip, from, to);
      const auto discrete = SampleDisplacement(motion, from, to);
      CHECK((paired.root_delta - discrete).GetLength() < 1e-6f);
      CHECK(paired.pose_time == Catch::Approx(std::clamp(to, 0.0f, 4.0f)));
      CHECK_FALSE(paired.looped);
    }
  }
}

TEST_CASE("discrete reverse intervals and looped paired intervals are distinct contracts",
          "[animation][sampling]") {
  auto clip = Track();
  const auto motion = ExtractMotionClip(clip);
  CHECK(SampleDisplacement(motion, 3.0f, 1.0f).GetLength() > 0.0f);
  CHECK(SampleAnimationStep(clip, 3.0f, 1.0f).root_delta == Vector3(0));

  clip.looping = true;
  const auto travel = clip.root_positions.back() - clip.root_positions.front();
  const auto cycles = SampleAnimationStep(clip, 0.0f, 12.0f);
  CHECK(cycles.root_delta == travel * 3.0f);
  CHECK(cycles.pose_time == 0.0f);
  CHECK(cycles.cycle_index == 3);
  CHECK(cycles.looped);
  CHECK(SampleDisplacement(motion, 0.0f, 12.0f) == travel);
}

TEST_CASE("paired motion does not invent a trajectory for a pose-only resource",
          "[animation][sampling]") {
  auto clip = Track();
  clip.poses[0].positions[BodyPart::player] = Vector3(0, 0, 0.2f);
  clip.root_positions.resize(1);
  CHECK(SampleAnimationStep(clip, 0.0f, 2.0f).root_delta == Vector3(0));
  CHECK(SampleAnimationStep(clip, 0.0f, 2.0f).continuous_time == 0.0f);
  CHECK(SamplePose(clip, 0.0f).positions[BodyPart::player].coords[2] == 0.2f);

  clip = Track();
  clip.poses.clear();
  CHECK(SampleAnimationStep(clip, 0.0f, 2.0f).root_delta == Vector3(0));
  CHECK(SampleRootPosition(clip.root_positions, 2.0f).GetLength() > 0.0f);
}

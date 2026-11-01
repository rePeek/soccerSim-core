// RM-2: root and pose come from one reference timeline.
//
// The sample must move the root by the clip's own displacement over the same
// interval it sampled the pose, including across a loop seam. A seam must never
// turn into a backward root jump just because the pose wrapped.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/animation/library.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/reference_sample.hpp"

using football::math::Vector3;

namespace {

AnimationClip StraightGait(int frames) {
  AnimationClip clip;
  clip.id = 1;
  clip.frame_count = static_cast<uint32_t>(frames);
  clip.metadata.action_type = e_DefString_Movement;
  clip.looping = true;
  for (int frame = 0; frame < frames; ++frame) {
    PoseFrame pose;
    pose.orientations[BodyPart::left_thigh].SetAngleAxis(
        0.01f * static_cast<float>(frame), Vector3(1, 0, 0));
    clip.poses.push_back(pose);
    clip.root_positions.push_back(Vector3(0, -0.25f * static_cast<float>(frame), 0));
  }
  return clip;
}

AnimationClip OneShot() {
  AnimationClip clip = StraightGait(5);
  clip.looping = false;
  clip.metadata.action_type = e_DefString_Shot;
  return clip;
}

}  // namespace

TEST_CASE("the root delta and pose use the same interval", "[animation][rm2]") {
  const AnimationClip clip = StraightGait(5);
  const ReferenceAnimationSample sample = SampleAnimationStep(clip, 0.0f, 1.0f);
  CHECK(sample.root_delta.coords[0] == Catch::Approx(0.0f));
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.25f));
  CHECK(sample.pose_time == Catch::Approx(1.0f));
  CHECK(sample.continuous_time == Catch::Approx(1.0f));
  CHECK_FALSE(sample.looped);
  // The pose is exactly the clip's sample at the same time.
  const PoseFrame expected = SamplePose(clip, 1.0f);
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    CHECK(sample.pose.orientations[joint].GetDotProduct(
              expected.orientations[joint]) == Catch::Approx(1.0f).margin(1e-6f));
  }
}

TEST_CASE("a loop seam advances the root instead of jumping back",
          "[animation][rm2]") {
  const AnimationClip clip = StraightGait(5);  // 4 frames per cycle, 1 m travel
  const ReferenceAnimationSample sample = SampleAnimationStep(clip, 3.5f, 5.5f);
  // 2 frames of travel at -0.25 m/frame.
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.5f).margin(1e-5f));
  CHECK(sample.pose_time == Catch::Approx(1.5f).margin(1e-5f));
  CHECK(sample.continuous_time == Catch::Approx(5.5f));
  CHECK(sample.looped);

  // A single full cycle advances by exactly the clip's total travel.
  const ReferenceAnimationSample full = SampleAnimationStep(clip, 0.0f, 4.0f);
  CHECK(full.root_delta.coords[1] == Catch::Approx(-1.0f).margin(1e-5f));
  CHECK(full.pose_time == Catch::Approx(0.0f).margin(1e-5f));
}

TEST_CASE("a high playback rate crossing several cycles stays additive",
          "[animation][rm2]") {
  const AnimationClip clip = StraightGait(5);
  const ReferenceAnimationSample many =
      SampleAnimationStep(clip, 0.5f, 12.5f);
  // 12 frames of travel, including three full cycles.
  CHECK(many.root_delta.coords[1] == Catch::Approx(-3.0f).margin(1e-4f));
  CHECK(many.pose_time == Catch::Approx(0.5f).margin(1e-5f));
  CHECK(many.looped);
}

TEST_CASE("a non-looping action clamps at the last sample", "[animation][rm2]") {
  const AnimationClip clip = OneShot();
  const ReferenceAnimationSample sample = SampleAnimationStep(clip, 2.0f, 50.0f);
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.5f).margin(1e-5f));
  CHECK(sample.pose_time == Catch::Approx(4.0f));
  CHECK_FALSE(sample.looped);
  const ReferenceAnimationSample past = SampleAnimationStep(clip, 4.0f, 8.0f);
  CHECK(past.root_delta.GetLength() == Catch::Approx(0.0f).margin(1e-6f));
}

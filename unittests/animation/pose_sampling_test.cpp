// ANIM-1 A1 — pure pose sampling tests (SamplePose).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "animation_test_support.hpp"
#include "football/animation/pose_sampling.hpp"

using football::math::Vector3;

namespace {
AnimationClip ThreeFrameClip() {
  AnimationClip clip;
  clip.id = 0;
  clip.frame_count = 3;
  clip.poses = {PoseWith(0.0f, 0.0f), PoseWith(0.5f, 90.0f), PoseWith(1.0f, 180.0f)};
  clip.root_positions = {Vector3(0, 0, 0), Vector3(0, -1, 0), Vector3(0, -2, 0)};
  return clip;
}
}  // namespace

TEST_CASE("integer frame times reproduce the baked frame exactly") {
  const AnimationClip clip = ThreeFrameClip();
  for (std::size_t f = 0; f < clip.poses.size(); ++f) {
    const PoseFrame sampled = SamplePose(clip, static_cast<float>(f));
    for (std::size_t b = 0; b < kBodyPartCount; ++b) {
      for (int e = 0; e < 4; ++e) {
        REQUIRE(sampled.orientations[b].elements[e] ==
                clip.poses[f].orientations[b].elements[e]);
      }
      for (int c = 0; c < 3; ++c) {
        REQUIRE(sampled.positions[b].coords[c] ==
                clip.poses[f].positions[b].coords[c]);
      }
    }
  }
}

TEST_CASE("fractional frame times slerp rotation and lerp root Z") {
  const AnimationClip clip = ThreeFrameClip();
  const PoseFrame mid = SamplePose(clip, 0.5f);
  REQUIRE(mid.positions[BodyPart::player].coords[2] == Catch::Approx(0.25f));

  const auto identity = football::math::Quaternion{};
  const float to_identity = mid.orientations[BodyPart::body].GetRotationAngle(identity);
  const float to_frame1 =
      mid.orientations[BodyPart::body].GetRotationAngle(clip.poses[1].orientations[BodyPart::body]);
  REQUIRE(to_identity == Catch::Approx(football::math::pi / 4.0f).margin(1e-3f));
  REQUIRE(to_frame1 == Catch::Approx(football::math::pi / 4.0f).margin(1e-3f));
}

TEST_CASE("out-of-range frame times clamp to the ends") {
  const AnimationClip clip = ThreeFrameClip();
  const PoseFrame low = SamplePose(clip, -5.0f);
  const PoseFrame high = SamplePose(clip, 99.0f);
  for (int e = 0; e < 4; ++e) {
    REQUIRE(low.orientations[BodyPart::body].elements[e] ==
            clip.poses.front().orientations[BodyPart::body].elements[e]);
    REQUIRE(high.orientations[BodyPart::body].elements[e] ==
            clip.poses.back().orientations[BodyPart::body].elements[e]);
  }
  REQUIRE(high.positions[BodyPart::player].coords[2] == Catch::Approx(1.0f));
}

TEST_CASE("an empty clip yields the default pose") {
  const AnimationClip empty;
  const PoseFrame pose = SamplePose(empty, 3.0f);
  for (std::size_t b = 0; b < kBodyPartCount; ++b) {
    for (int e = 0; e < 4; ++e) REQUIRE(std::isfinite(pose.orientations[b].elements[e]));
  }
}

TEST_CASE("sampling is deterministic and finite") {
  const AnimationClip clip = ThreeFrameClip();
  for (const float t : {0.1f, 0.5f, 1.25f, 1.9f}) {
    const PoseFrame a = SamplePose(clip, t);
    const PoseFrame b = SamplePose(clip, t);
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      for (int e = 0; e < 4; ++e) {
        REQUIRE(a.orientations[j].elements[e] == b.orientations[j].elements[e]);
        REQUIRE(std::isfinite(a.orientations[j].elements[e]));
      }
    }
  }
}

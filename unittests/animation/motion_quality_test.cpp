// RMA-1 — clip motion quality: does the source root track agree with the foot
// the support derivation says is planted, and does the loop seam pop?
//
// The derivation is pure animation-domain work. These tests use a synthetic
// clip whose root and foot motion are known exactly, plus the shipped catalog
// as the "what does the real animation library report" check.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/animation/foot_support.hpp"
#include "football/animation/library.hpp"

using football::math::Vector3;

namespace {

// Four frames of a perfectly repeated pose with an explicit stance track. The
// root track decides whether the planted foot slides.
AnimationClip StanceClip(const Vector3& per_frame_root) {
  AnimationClip clip;
  clip.id = 1;
  clip.frame_count = 4;
  clip.metadata.action_type = e_DefString_Movement;
  for (int f = 0; f < 4; ++f) {
    clip.poses.push_back(PoseFrame{});
    clip.root_positions.push_back(per_frame_root * static_cast<float>(f));
    clip.foot_support.push_back(FootSupportFrame{FootPhase::Stance,
                                                 FootPhase::Swing});
  }
  clip.stance_speed = std::sqrt(per_frame_root.coords[0] * per_frame_root.coords[0] +
                                per_frame_root.coords[1] * per_frame_root.coords[1]) /
                      0.01f;
  return clip;
}

}  // namespace

TEST_CASE("motion quality reports a stance foot dragged by the root track",
          "[animation][motionquality]") {
  // Root advances 2 cm per frame (2 m/s) while the stance pose never moves the
  // ankle: the foot is planted in the skeleton but dragged by the root.
  AnimationClip clip = StanceClip(Vector3(0, -0.02f, 0));
  DeriveMotionQuality(clip);
  CHECK(clip.motion_quality.tracked);
  CHECK(clip.motion_quality.root_speed == Catch::Approx(2.0f).margin(1e-4f));
  CHECK(clip.motion_quality.support_slip_speed ==
        Catch::Approx(2.0f).margin(1e-4f));
  CHECK(clip.motion_quality.worst_support_slip_speed ==
        Catch::Approx(2.0f).margin(1e-4f));
  CHECK_FALSE(clip.motion_quality.support_consistent);
  CHECK_FALSE(clip.motion_quality.ik_free_usable);
  // The support track is still readable, so the Player can calibrate the root
  // from the stance speed instead of the authored root track.
  CHECK(clip.motion_quality.stance_calibrated);
}

TEST_CASE("a stationary root with a planted foot has no slide",
          "[animation][motionquality]") {
  AnimationClip clip = StanceClip(Vector3(0));
  DeriveMotionQuality(clip);
  CHECK(clip.motion_quality.tracked);
  CHECK(clip.motion_quality.root_speed == Catch::Approx(0.0f).margin(1e-6f));
  CHECK(clip.motion_quality.support_slip_speed ==
        Catch::Approx(0.0f).margin(1e-6f));
  CHECK(clip.motion_quality.support_consistent);
  // A clip with no forward travel is not a locomotion gait to adapt.
  CHECK_FALSE(clip.motion_quality.ik_free_usable);
}

TEST_CASE("motion quality reports a loop seam pose pop",
          "[animation][motionquality]") {
  AnimationClip clip = StanceClip(Vector3(0));
  // Turn one joint a quarter turn on the last frame: the loop would snap.
  clip.poses.back().orientations[BodyPart::left_thigh].SetAngleAxis(
      1.4f, Vector3(0, 0, 1));
  DeriveMotionQuality(clip);
  CHECK(clip.motion_quality.seam_pose_step > kIkFreeMaxSeamPoseStep);
  CHECK_FALSE(clip.motion_quality.loop_continuous);
  CHECK_FALSE(clip.motion_quality.ik_free_usable);
}

TEST_CASE("the shipped catalog reports a consistent quality census",
          "[animation][motionquality][catalog]") {
  AnimationLibrary library;
  REQUIRE(library.LoadCatalog(GFOOTBALL_ANIMATION_CATALOG_PATH));
  const AnimationLibrary::MotionQualityCensus census = library.QualityCensus();
  REQUIRE(census.total > 0);
  REQUIRE(census.tracked > 0);
  CHECK(census.ik_free_usable <= census.tracked);
  CHECK(census.stance_calibrated <= census.tracked);
  CHECK(census.ik_free_usable <= census.stance_calibrated);
  CHECK(census.support_slip <= census.tracked);
  CHECK(census.loop_discontinuous <= census.tracked);
  for (const AnimationClip& clip : library.Clips()) {
    if (!clip.motion_quality.tracked) continue;
    if (clip.motion_quality.ik_free_usable) {
      CHECK(clip.motion_quality.support_consistent);
      CHECK(clip.motion_quality.loop_continuous);
    }
    // The verification is a measurement, never a clamp of the source track.
    CHECK(clip.motion_quality.support_slip_speed >= 0.0f);
    CHECK(std::isfinite(clip.motion_quality.seam_root_speed_delta));
  }
}

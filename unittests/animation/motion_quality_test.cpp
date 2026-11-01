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

// A 2 m/s straight gait whose stance ankle travels backward at the same speed.
// This is what the in-memory stance-speed calibration can execute.
AnimationClip CalibratedStanceClip() {
  AnimationClip clip;
  clip.id = 2;
  clip.frame_count = 8;
  clip.metadata.action_type = e_DefString_Movement;
  for (int f = 0; f < 8; ++f) {
    PoseFrame pose;
    // Rotating the thigh about +X moves the ankle forward in +Y; 0.023 rad per
    // frame is about 2 cm, matching the root's 2 cm/frame backward travel.
    pose.orientations[BodyPart::left_thigh].SetAngleAxis(
        0.023f * static_cast<float>(f), Vector3(1, 0, 0));
    clip.poses.push_back(pose);
    clip.root_positions.push_back(Vector3(0, -0.02f * static_cast<float>(f), 0));
    clip.foot_support.push_back(FootSupportFrame{FootPhase::Stance,
                                                 FootPhase::Swing});
  }
  clip.stance_speed = 2.0f;
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
  // The foot does not travel backward, so calibration cannot claim it either.
  CHECK_FALSE(clip.motion_quality.stance_calibrated);
}

TEST_CASE("a calibrated straight gait is IK-free usable",
          "[animation][motionquality]") {
  AnimationClip clip = CalibratedStanceClip();
  DeriveMotionQuality(clip);
  CHECK(clip.motion_quality.root_speed == Catch::Approx(2.0f).margin(1e-4f));
  CHECK(clip.motion_quality.stance_speed == Catch::Approx(2.0f).margin(1e-4f));
  CHECK(clip.motion_quality.stance_direction_alignment > 0.99f);
  CHECK(clip.motion_quality.support_consistent);
  CHECK(clip.motion_quality.stance_calibrated);
  CHECK(clip.motion_quality.ik_free_usable);
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
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
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

// RM-0: the strict gate is only useful if a refusal is explainable.
// This test makes the zero-usable baseline a measurement instead of a
// comment: every ordinary movement clip is either usable or lands in at
// least one measured failure bucket, and the census equals the per-clip
// verdicts.
TEST_CASE("RM-0: every movement clip's gate verdict has a measured reason",
          "[animation][motionquality][catalog][rm0]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  const AnimationLibrary::MotionQualityCensus census =
      library.QualityCensus();
  REQUIRE(census.movement > 0);
  INFO("movement=" << census.movement
       << " usable=" << census.ik_free_usable
       << " rejected=" << census.movement_rejected
       << " untracked=" << census.movement_untracked
       << " untrusted=" << census.movement_support_untrusted
       << " loop=" << census.movement_loop_discontinuous
       << " misaligned=" << census.movement_stance_misaligned
       << " slip=" << census.movement_support_slip
       << " slow_root=" << census.movement_slow_root
       << " slow_stance=" << census.movement_slow_stance);
  CHECK(census.movement == census.ik_free_usable + census.movement_rejected);
  CHECK(census.movement_untracked <= census.movement);
  CHECK(census.movement_support_untrusted <= census.movement);
  CHECK(census.movement_loop_discontinuous <= census.movement);
  CHECK(census.movement_stance_misaligned <= census.movement);
  CHECK(census.movement_support_slip <= census.movement);
  // A rejected clip must name a reason; a usable clip must not be rejected.
  std::size_t explained = 0;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    const bool usable = clip.motion_quality.ik_free_usable;
    const bool reason =
        !clip.motion_quality.tracked ||
        !FootSupportTrusted(clip.foot_support_summary,
                            !clip.foot_support.empty()) ||
        !clip.motion_quality.loop_continuous ||
        clip.motion_quality.stance_direction_alignment < 0.9f ||
        !clip.motion_quality.support_consistent ||
        clip.motion_quality.root_speed <= 0.5f ||
        clip.motion_quality.stance_speed <= 0.5f;
    // The strict gate is exactly this reason set for a movement clip: a
    // usable clip has no failing measurement, a rejected one names at least
    // one.
    CHECK(usable == !reason);
    if (!usable && reason) ++explained;
  }
  CHECK(explained == census.movement_rejected);
}

// RM-1: the import-time gait repair must produce a real, playable cycle, not
// just move the gate. A repaired clip is a GLB source (not "autogen") whose
// frame count grew from the authored half-cycle, whose root speed is constant
// across the cycle, and whose stance foot is planted in the clip's own world
// track.
TEST_CASE("RM-1: a real GLB gait is repaired into an IK-free cycle",
          "[animation][motionquality][catalog][rm1]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  int real_glb = 0;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;
    if (!clip.motion_quality.ik_free_usable) continue;
    if (clip.name.starts_with("autogen")) continue;
    ++real_glb;
    CHECK(clip.frame_count > 25);
    CHECK(clip.motion_quality.support_consistent);
    CHECK(clip.motion_quality.loop_continuous);
    CHECK(clip.motion_quality.stance_direction_alignment > 0.9f);
    // The retime makes the root a constant-speed track. One GLB frame of
    // tolerance covers the float accumulation in the cumulative positions.
    REQUIRE(clip.root_positions.size() >= 3);
    float shortest = 1e9f;
    float longest = 0.0f;
    for (std::size_t frame = 0; frame + 1 < clip.root_positions.size();
         ++frame) {
      const float step =
          (clip.root_positions[frame + 1] - clip.root_positions[frame])
              .Get2D()
              .GetLength();
      shortest = std::min(shortest, step);
      longest = std::max(longest, step);
    }
    INFO("clip " << clip.name << " step " << shortest << ".." << longest);
    CHECK(longest - shortest <= 1e-4f);
  }
  CHECK(real_glb > 0);
}

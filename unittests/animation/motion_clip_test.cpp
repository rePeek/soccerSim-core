// Motion reference extraction from baked animation clips.
//
// The extracted MotionClip is *reference* data: it must reproduce the baked root
// trajectory frame by frame while leaving the source AnimationClip untouched
// (selection, metadata and touch records depend on it), and it must not carry the
// pelvis bob, which belongs to the animation root pose.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "football/animation/motion_clip.hpp"

using football::math::Vector3;
using Catch::Approx;

namespace {

// A baked clip with the given per-frame root positions. Only the root motion
// matters here; poses are irrelevant to extraction and stay empty.
AnimationClip ClipWithRootPositions(std::vector<Vector3> positions) {
  AnimationClip clip;
  clip.name = "test";
  clip.frame_count = static_cast<uint32_t>(positions.size());
  clip.root_positions = std::move(positions);
  return clip;
}

// A straight run at a constant per-tick displacement (7 m/s at 100 Hz).
AnimationClip RunningClip(int frames, float metres_per_tick) {
  std::vector<Vector3> positions;
  positions.reserve(static_cast<std::size_t>(frames));
  for (int frame = 0; frame < frames; ++frame) {
    positions.push_back(
        Vector3(metres_per_tick * static_cast<float>(frame), 0.0f,
                0.01f * static_cast<float>(frame % 2)));
  }
  return ClipWithRootPositions(std::move(positions));
}

// A shot: the actor is still travelling, then decelerates as it strikes.
AnimationClip ShootingClip() {
  const float x[] = {0.0f, 0.05f, 0.095f, 0.13f, 0.155f, 0.17f, 0.18f, 0.185f};
  std::vector<Vector3> positions;
  for (float value : x) positions.push_back(Vector3(value, 0.0f, 0.004f));
  return ClipWithRootPositions(std::move(positions));
}

float SpeedOf(const MotionClip& clip, std::size_t frame, float dt) {
  return clip.frames[frame].local_displacement.GetLength() / dt;
}

}  // namespace

TEST_CASE("Running clip extraction reproduces the baked per-tick displacement",
          "[unit][animation][motion]") {
  constexpr float kDt = 0.01f;
  const AnimationClip clip = RunningClip(8, 0.07f);
  const MotionClip motion = ExtractMotionClip(clip);

  REQUIRE(motion.size() == clip.root_positions.size());
  // Frame 0 is the origin of the extraction, not a movement.
  CHECK(motion.frames[0].local_displacement == Vector3(0, 0, 0));

  for (std::size_t frame = 1; frame < motion.size(); ++frame) {
    const Vector3 expected =
        clip.root_positions[frame] - clip.root_positions[frame - 1];
    CHECK(motion.frames[frame].local_displacement.coords[0] ==
          Approx(expected.coords[0]));
    CHECK(motion.frames[frame].local_displacement.coords[1] ==
          Approx(expected.coords[1]));
    CHECK(SpeedOf(motion, frame, kDt) == Approx(7.0f).margin(1e-4f));
  }

  // The cumulative reference reproduces the baked trajectory.
  for (std::size_t frame = 1; frame < motion.size(); ++frame) {
    const Vector3 reconstructed = ReferencePosition(motion, frame);
    CHECK(reconstructed.coords[0] ==
          Approx(clip.root_positions[frame].coords[0]).margin(1e-5f));
    CHECK(reconstructed.coords[1] ==
          Approx(clip.root_positions[frame].coords[1]).margin(1e-5f));
  }
  CHECK(ReferencePosition(motion, motion.size()).coords[0] ==
        Approx(clip.root_positions.back().coords[0]).margin(1e-5f));
}

TEST_CASE("Shooting clip extraction keeps the deceleration profile",
          "[unit][animation][motion]") {
  constexpr float kDt = 0.01f;
  const AnimationClip clip = ShootingClip();
  const MotionClip motion = ExtractMotionClip(clip);

  REQUIRE(motion.size() == clip.root_positions.size());
  // Still moving at the start of the shot, nearly stopped at the strike.
  CHECK(SpeedOf(motion, 1, kDt) == Approx(5.0f).margin(1e-3f));
  CHECK(SpeedOf(motion, motion.size() - 1, kDt) ==
        Approx(0.5f).margin(1e-3f));
  for (std::size_t frame = 2; frame < motion.size(); ++frame) {
    CHECK(SpeedOf(motion, frame, kDt) <= SpeedOf(motion, frame - 1, kDt) + 1e-6f);
  }
  // Total travel matches the baked end position.
  CHECK(ReferencePosition(motion, motion.size()).coords[0] ==
        Approx(clip.root_positions.back().coords[0]).margin(1e-5f));
}

TEST_CASE("Extraction drops the pelvis bob and leaves the clip untouched",
          "[unit][animation][motion]") {
  const AnimationClip clip = RunningClip(5, 0.07f);
  const std::vector<Vector3> original = clip.root_positions;
  const MotionClip motion = ExtractMotionClip(clip);

  // The baked root z is the animation bob; it stays with the pose, not here.
  for (const MotionFrame& frame : motion.frames) {
    CHECK(frame.local_displacement.coords[2] == Approx(0.0f));
  }
  CHECK(motion.frames[1].local_displacement.coords[2] == Approx(0.0f));
  // The source clip is read-only reference data.
  REQUIRE(clip.root_positions.size() == original.size());
  for (std::size_t i = 0; i < original.size(); ++i) {
    CHECK(clip.root_positions[i].coords[0] == Approx(original[i].coords[0]));
    CHECK(clip.root_positions[i].coords[1] == Approx(original[i].coords[1]));
    CHECK(clip.root_positions[i].coords[2] == Approx(original[i].coords[2]));
  }
}

TEST_CASE("Extraction handles degenerate clips", "[unit][animation][motion]") {
  const AnimationClip empty;
  const MotionClip motion = ExtractMotionClip(empty);
  CHECK(motion.empty());
  CHECK(ReferencePosition(motion, 0) == Vector3(0));

  const AnimationClip single = ClipWithRootPositions({Vector3(3, 4, 5)});
  const MotionClip single_motion = ExtractMotionClip(single);
  REQUIRE(single_motion.size() == 1);
  CHECK(single_motion.frames[0].local_displacement == Vector3(0, 0, 0));
  CHECK(ReferencePosition(single_motion, 0) == Vector3(0));
  CHECK(ReferencePosition(single_motion, 1) == Vector3(0));  // past the end clamps

  const AnimationClip stationary = ClipWithRootPositions(
      {Vector3(1, 2, 0), Vector3(1, 2, 0.02f), Vector3(1, 2, 0)});
  const MotionClip stationary_motion = ExtractMotionClip(stationary);
  for (const MotionFrame& frame : stationary_motion.frames) {
    CHECK(frame.local_displacement.GetLength() == Approx(0.0f));
  }
}

TEST_CASE("Frame index and frame time are unambiguous",
          "[unit][animation][motion]") {
  const AnimationClip clip = RunningClip(5, 0.07f);  // frames 0..4
  const MotionClip motion = ExtractMotionClip(clip);
  REQUIRE(motion.size() == 5);
  CHECK(motion.last_frame_time() == Approx(4.0f));

  // Frame index 0 is the origin; index i is the reference *at* that frame.
  CHECK(ReferencePosition(motion, 0) == Vector3(0));
  for (std::size_t frame = 0; frame < motion.size(); ++frame) {
    CHECK(ReferencePosition(motion, frame).coords[0] ==
          Approx(clip.root_positions[frame].coords[0]).margin(1e-5f));
  }
  // Past the last frame the reference holds its final position: a finished clip
  // must not keep accumulating the last displacement.
  const Vector3 end = ReferencePosition(motion, motion.size() - 1);
  CHECK(ReferencePosition(motion, motion.size() + 10) == end);
  CHECK(ReferencePositionAt(motion, 99.0f).coords[0] == Approx(end.coords[0]));
  CHECK(ReferencePositionAt(motion, -3.0f) == Vector3(0));

  // Frame time interpolates between frame samples.
  CHECK(ReferencePositionAt(motion, 2.0f) == ReferencePosition(motion, 2));
  CHECK(ReferencePositionAt(motion, 2.5f).coords[0] == Approx(0.175f).margin(1e-5f));
}

TEST_CASE("Displacement sampling is additive, rate-independent and clamps",
          "[unit][animation][motion]") {
  constexpr float kFramesPerTick = 1.0f;  // 1.0 == normal playback at 100 Hz
  const AnimationClip clip = RunningClip(11, 0.07f);  // 0 .. 10
  const MotionClip motion = ExtractMotionClip(clip);

  // One tick at 1.0x advances one frame.
  const Vector3 one = SampleDisplacement(motion, 0.0f, kFramesPerTick);
  CHECK(one.coords[0] == Approx(0.07f).margin(1e-6f));

  // Rates scale the time the caller advances, not the sampled result.
  CHECK(SampleDisplacement(motion, 0.0f, 0.5f).coords[0] ==
        Approx(0.035f).margin(1e-6f));   // 0.5x playback
  CHECK(SampleDisplacement(motion, 0.0f, 1.5f).coords[0] ==
        Approx(0.105f).margin(1e-6f));   // 1.5x playback

  // A tick that spans several frames equals the sum of single-frame parts.
  const Vector3 span = SampleDisplacement(motion, 2.0f, 7.0f);
  Vector3 split(0);
  for (float t = 2.0f; t < 7.0f; t += 1.0f) {
    split += SampleDisplacement(motion, t, t + 1.0f);
  }
  CHECK(span.coords[0] == Approx(split.coords[0]).margin(1e-6f));
  CHECK(span.coords[0] == Approx(0.35f).margin(1e-6f));

  // Splitting at a fractional boundary is still additive.
  const Vector3 whole = SampleDisplacement(motion, 1.25f, 4.75f);
  const Vector3 first = SampleDisplacement(motion, 1.25f, 3.0f);
  const Vector3 second = SampleDisplacement(motion, 3.0f, 4.75f);
  CHECK(whole.coords[0] == Approx(first.coords[0] + second.coords[0]).margin(1e-6f));

  // Sampling past the end adds nothing more.
  CHECK(SampleDisplacement(motion, 10.0f, 25.0f) == Vector3(0));
  CHECK(SampleDisplacement(motion, 0.0f, 100.0f).coords[0] ==
        Approx(ReferencePosition(motion, motion.size() - 1).coords[0]).margin(1e-6f));
  // The motion is planar: sampling never produces a z displacement.
  CHECK(SampleDisplacement(motion, 0.0f, 5.0f).coords[2] == Approx(0.0f));
}

// ANIM-1 A3 — Player-owned animation playback state tests.
//
// Pure: no Simulation, Humanoid, Ball, Referee or RNG. Small hand-built clips,
// exactly the Player-side playback semantics.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "animation_test_support.hpp"
#include "football/animation/playback.hpp"
using namespace football::animation;

using football::math::Vector3;

namespace {
AnimationClip Clip(uint32_t id, float root_z, uint32_t frames = 6) {
  AnimationClip clip;
  clip.id = id;
  clip.frame_count = frames;
  clip.poses.resize(frames);
  for (uint32_t f = 0; f < frames; ++f) {
    clip.poses[f] = PoseWith(root_z + 0.1f * static_cast<float>(f),
                             static_cast<float>(f) * 15.0f);
  }
  clip.root_positions.assign(frames, Vector3(0, 0, 0));
  return clip;
}
}  // namespace

TEST_CASE("playback advances one frame per tick within a clip") {
  PlaybackState playback;
  const AnimationClip clip = Clip(1, 0.0f);
  for (int i = 0; i < 4; ++i) {
    const PoseFrame& pose = playback.Tick(clip, kDefaultMovementBlendTicks);
    CHECK(playback.clip_id == 1);
    CHECK(playback.frame == i + 1);
    CHECK(std::isfinite(pose.orientations[BodyPart::body].elements[3]));
  }
}

TEST_CASE("a clip change blends from the previous final pose") {
  PlaybackState playback;
  const AnimationClip a = Clip(1, 0.0f);
  const AnimationClip b = Clip(2, 0.5f);
  for (int i = 0; i < 4; ++i) playback.Tick(a, kDefaultMovementBlendTicks);

  // First tick of the new clip: still close to the previous pose (blending),
  // and the cursor resets to frame 0 for the new clip.
  const float before = playback.final_pose.positions[BodyPart::player].coords[2];
  const PoseFrame& first = playback.Tick(b, kActionBlendTicks);
  CHECK(playback.clip_id == 2);
  CHECK(playback.frame == 1);
  const float after = first.positions[BodyPart::player].coords[2];
  // Root Z moved toward the new clip but did not jump to the target instantly.
  CHECK(after >= before - 1e-4f);
  CHECK(after < 0.5f);
}

TEST_CASE("reset clears the playback state") {
  PlaybackState playback;
  const AnimationClip clip = Clip(1, 0.0f);
  for (int i = 0; i < 3; ++i) playback.Tick(clip, kDefaultMovementBlendTicks);
  REQUIRE(playback.have_pose);
  playback.Reset();
  CHECK_FALSE(playback.have_pose);
  CHECK(playback.clip_id == -1);
  CHECK(playback.frame == 0);
}

TEST_CASE("playback is deterministic for equal input") {
  const AnimationClip a = Clip(1, 0.0f);
  const AnimationClip b = Clip(2, 0.5f);
  const auto run = [&]() {
    PlaybackState p;
    for (int i = 0; i < 5; ++i) p.Tick(a, kDefaultMovementBlendTicks);
    for (int i = 0; i < 5; ++i) p.Tick(b, kActionBlendTicks);
    return p.final_pose;
  };
  const PoseFrame x = run();
  const PoseFrame y = run();
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    for (int e = 0; e < 4; ++e) {
      REQUIRE(x.orientations[j].elements[e] == y.orientations[j].elements[e]);
    }
    for (int c = 0; c < 3; ++c) {
      REQUIRE(x.positions[j].coords[c] == y.positions[j].coords[c]);
    }
  }
}

// ANIM-2 A13.0 — pure pose-based entry-frame matching.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "animation_test_support.hpp"
#include "football/animation/entry_frame.hpp"
#include "football/animation/match.hpp"

using namespace football::animation;
using football::math::Vector3;

namespace {

// A clip whose every frame has a distinct pose, so the closest frame is unique.
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

MatchQuery MovementQuery() {
  MatchQuery query;
  query.function_type = e_FunctionType_Movement;
  query.constraints.crude.byFunctionType = true;
  query.constraints.crude.functionType = e_FunctionType_Movement;
  query.constraints.crude.byIncomingVelocity = true;
  query.constraints.crude.incomingVelocity = e_Velocity_Idle;
  query.constraints.crude.incomingVelocity_Strict = true;
  query.constraints.crude.byIncomingBodyDirection = true;
  query.constraints.crude.incomingBodyDirection = Vector3(0, -1, 0);
  query.constraints.crude.incomingBodyDirection_Strict = true;
  query.refinement.rel_desired_direction = Vector3(0, -1, 0);
  query.refinement.desired_velocity = e_Velocity_Idle;
  query.refinement.look_at = Vector3(0, -10, 0);
  query.refinement.position = Vector3(0, 0, 0);
  query.refinement.rel_incoming_body_direction = Vector3(0, -1, 0);
  query.refinement.incoming_velocity = e_Velocity_Idle;
  query.refinement.desired_foot = e_Foot_Left;
  query.refinement.is_movement = true;
  return query;
}

}  // namespace

TEST_CASE("PoseDistance is near zero for an identical pose and positive otherwise") {
  const PoseFrame a = PoseWith(0.2f, 30.0f);
  const PoseFrame b = PoseWith(0.2f, 30.0f);
  const PoseFrame c = PoseWith(0.2f, 90.0f);
  // GetRotationAngle carries a small numeric epsilon even for identical
  // quaternions, so an identical pose is near-zero, not bit-exact zero.
  CHECK(PoseDistance(a, b) < 1e-3f);
  CHECK(PoseDistance(a, c) > 0.1f);
}

TEST_CASE("SelectEntryFrame picks the frame whose pose matches the reference") {
  const AnimationClip clip = PosesClip(7, 12);
  EntryFrameRequest request;
  request.reference_pose = &clip.poses[5];
  request.min_remaining_frames = 1;
  const EntryFrameResult result = SelectEntryFrame(clip, request);
  REQUIRE(result.matched);
  CHECK(result.frame == 5);
  CHECK(result.pose_distance < 1e-2f);
}

TEST_CASE("SelectEntryFrame respects the minimum remaining frames") {
  const AnimationClip clip = PosesClip(8, 12);
  EntryFrameRequest request;
  request.reference_pose = &clip.poses[11];
  request.min_remaining_frames = 4;  // last legal entry = 12 - 1 - 4 = 7
  const EntryFrameResult result = SelectEntryFrame(clip, request);
  REQUIRE(result.matched);
  CHECK(result.frame <= 7);
}

TEST_CASE("SelectEntryFrame keeps frame 0 without a legal request") {
  const AnimationClip clip = PosesClip(9, 12);

  EntryFrameRequest no_pose;
  CHECK_FALSE(SelectEntryFrame(clip, no_pose).matched);

  AnimationClip empty;
  EntryFrameRequest request;
  request.reference_pose = &clip.poses[3];
  CHECK_FALSE(SelectEntryFrame(empty, request).matched);

  EntryFrameRequest too_late;
  too_late.reference_pose = &clip.poses[3];
  too_late.min_remaining_frames = 12;  // no legal frame remains
  CHECK_FALSE(SelectEntryFrame(clip, too_late).matched);
}

TEST_CASE("Match applies pose-based entry-frame matching only when enabled") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  const auto plain = Match(library, MovementQuery());
  REQUIRE(plain.has_value());
  CHECK(plain->start_frame == 0);

  const AnimationClip& clip = library.Get(static_cast<uint32_t>(plain->clip_id));
  constexpr int kExpectedFrame = 7;
  REQUIRE(static_cast<int>(clip.poses.size()) > kExpectedFrame + 2);

  MatchQuery query = MovementQuery();
  query.entry.enabled = true;
  query.entry.reference_pose = clip.poses[kExpectedFrame];
  const auto matched = Match(library, query);
  REQUIRE(matched.has_value());
  CHECK(matched->clip_id == plain->clip_id);
  CHECK(matched->start_frame == kExpectedFrame);
}

TEST_CASE("SelectEntryFrame never enters at or after a touch frame") {
  const AnimationClip clip = PosesClip(11, 20);
  constexpr int kTouchFrame = 10;
  EntryFrameRequest request;
  // The pose is closest to a late frame, but the touch bound must win.
  request.reference_pose = &clip.poses[kTouchFrame + 5];
  request.min_remaining_frames = 1;
  request.last_legal_frame = kTouchFrame - 1;
  const EntryFrameResult result = SelectEntryFrame(clip, request);
  REQUIRE(result.matched);
  CHECK(result.frame <= kTouchFrame - 1);
}

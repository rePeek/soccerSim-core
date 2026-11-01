// RM-F1: root-driven and compatibility playback share one continuous phase.
//
// The old code kept a wrapped playback cursor and a separate unwrapped root
// cursor. The compatibility path advanced only the wrapped one, so returning to
// the root path re-sampled a stale phase. This test drives the real production
// composition through root -> fallback -> root across a loop seam and checks
// that the cursor and the sampled pose stay continuous.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "football/animation/library.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_motion.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

constexpr float kTick = 0.01f;

PlayerBodyProfile Body() {
  PlayerDefinition definition;
  definition.height = 1.80f;
  definition.weight = 75.0f;
  return MakePlayerBodyProfile(definition);
}

PlayerMotionProfile Profile() {
  return PlayerMotionProfile{8.0f, 6.0f, 12.0f, 6.0f};
}

float MaxPoseStep(const PoseFrame& before, const PoseFrame& after) {
  float step = 0.0f;
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    auto a = before.orientations[joint];
    auto b = after.orientations[joint];
    const float dot = a.MakeSameNeighborhood(b);
    step = std::max(step,
                    2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f)));
  }
  return step;
}

}  // namespace

TEST_CASE("RM-F1: root -> fallback -> root preserves the animation phase",
          "[loco][ikfree][catalog][rmf1]") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_ANIMATION_CATALOG_PATH));
  const AnimationClip* clip = nullptr;
  for (const AnimationClip& candidate : library.Clips()) {
    if (candidate.metadata.action_type != e_DefString_Movement) continue;
    if (!candidate.motion_quality.ik_free_usable) continue;
    if (candidate.name.starts_with("autogen")) continue;
    if (candidate.name.find("/movement/sprint/") == std::string::npos) continue;
    clip = &candidate;
    break;
  }
  REQUIRE(clip != nullptr);
  const float period =
      static_cast<float>(std::max<std::size_t>(clip->root_positions.size() - 1, 1));
  const PlayerBodyProfile body = Body();
  const PlayerMotionProfile profile = Profile();
  const float native_speed =
      LocomotionClipReferenceSpeed(*clip) * body.height_scale;

  PlayerActionControl control;
  control.desired_velocity = Vector3(0, -native_speed, 0);
  control.idle_facing = Vector3(0, -1, 0);
  control.authority = PlayerMotionAuthority::Locomotion;
  PlayerKinematicState state;
  state.bodyFacing = Vector3(0, -1, 0);
  state.velocity = control.desired_velocity;

  // Start just before the loop seam so the fallback run crosses it.
  float continuous = period - 2.0f;
  PlayerPlaybackState playback;
  playback.frame_time = continuous;
  LocomotionPlaybackRequest wrapped;
  wrapped.clip_id = clip->id;
  wrapped.frame_time = std::fmod(continuous, period);

  float previous_continuous = continuous;
  auto require_cursor = [&](float next) {
    CHECK(next >= previous_continuous - 1e-4f);
    CHECK(next - previous_continuous <=
          2.2f * kTick * 100.0f + 1e-3f);
    previous_continuous = next;
  };

  // Root-driven half-cycle.
  for (int tick = 0; tick < 6; ++tick) {
    const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
        *clip, state, profile, body, control, playback, kTick);
    REQUIRE(sample.feasible);
    state.position += sample.root_delta;
    state.velocity = sample.velocity;
    continuous = sample.continuous_clip_time;
    require_cursor(continuous);
    playback.frame_time = continuous;
    wrapped.frame_time = sample.clip_time;
  }

  PoseFrame last_pose = SamplePose(*clip, std::fmod(continuous, period));

  // Compatibility half-cycle: the motion solver supplies displacement, but the
  // animation phase advances through the same continuous cursor.
  for (int tick = 0; tick < 12; ++tick) {
    state = PlayerMotion::StepContinuous(state, profile, control.desired_velocity,
                                         control.idle_facing, kTick)
                .state;
    const float achieved_speed = state.velocity.GetLength();
    const float reference_speed =
        LocomotionEffectiveGaitSpeed(*clip, body.height_scale);
    const LocomotionPlaybackRequest previous = wrapped;
    wrapped = AdvanceLocomotionPlayback(previous, clip->id, *clip, achieved_speed,
                                        reference_speed, 0.0f, kTick, 1.0f);
    continuous = AdvanceContinuousLocomotionTime(
        continuous, previous.clip_id, clip->id, previous.frame_time,
        wrapped.frame_time, period);
    require_cursor(continuous);
    playback.frame_time = continuous;
  }
  last_pose = SamplePose(*clip, wrapped.frame_time);

  // Root-driven again. The first sample must continue the fallback phase, not
  // restart from the phase the first root run ended at.
  for (int tick = 0; tick < 12; ++tick) {
    const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
        *clip, state, profile, body, control, playback, kTick);
    REQUIRE(sample.feasible);
    const PoseFrame sampled = SamplePose(*clip, sample.clip_time);
    const float step = MaxPoseStep(last_pose, sampled);
    // One ordinary animation frame at the repaired cadence. The fallback and
    // the root path must not introduce a seam pop.
    CHECK(step <= 0.35f);
    last_pose = sampled;
    state.position += sample.root_delta;
    state.velocity = sample.velocity;
    continuous = sample.continuous_clip_time;
    require_cursor(continuous);
    playback.frame_time = continuous;
    wrapped.frame_time = sample.clip_time;
  }
  CHECK(continuous > period);
}

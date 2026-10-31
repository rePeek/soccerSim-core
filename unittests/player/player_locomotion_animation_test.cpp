// LOCO-2.2 — read-only locomotion/animation adapter interface.
//
// Pure value contract: the input is a snapshot of kinematic state + desired
// velocity + body profile; the output only proposes clip/rate/time and can
// never move the player. No Simulation, Humanoid, RNG or animation asset.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;
using football::player::MakePlayerBodyProfile;
using football::player::PlayerBodyProfile;
using football::player::PlayerDefinition;

namespace {

PlayerBodyProfile Profile(float height, float mass) {
  PlayerDefinition d;
  d.height = height;
  d.weight = mass;
  return MakePlayerBodyProfile(d);
}

}  // namespace

TEST_CASE("adapter input is a deterministic value snapshot") {
  PlayerKinematicState current;
  current.position = Vector3(1.0f, 2.0f, 0.0f);
  current.velocity = Vector3(0.0f, -3.0f, 0.0f);
  current.bodyFacing = Vector3(0.0f, -1.0f, 0.0f);

  PlayerKinematicState next = current;
  next.position += Vector3(0.0f, -0.03f, 0.0f);

  const auto body = Profile(1.8f, 75.0f);
  const Vector3 desired(0.0f, -4.0f, 0.0f);
  const auto input = MakeLocomotionAnimationInput(current, next, desired, body);
  const auto again = MakeLocomotionAnimationInput(current, next, desired, body);

  // Deterministic construction from equal inputs.
  REQUIRE(input.current.position.coords[0] == again.current.position.coords[0]);
  REQUIRE(input.body.girth_scale == again.body.girth_scale);

  // Copies, not references: mutating the source cannot leak into the snapshot.
  current.position.coords[0] = 99.0f;
  REQUIRE(input.current.position.coords[0] == Catch::Approx(1.0f));
  REQUIRE(input.next.position.coords[1] == Catch::Approx(2.0f - 0.03f));
  REQUIRE(input.desired_velocity.coords[1] == Catch::Approx(-4.0f));
  REQUIRE(input.body.height_scale == body.height_scale);
  REQUIRE(input.body.girth_scale == body.girth_scale);
  REQUIRE(input.body.girth_scale == again.body.girth_scale);
}

TEST_CASE("adapter output defaults are inert and never carry a position") {
  LocomotionAnimationOutput output;
  REQUIRE(output.animation_id == -1);
  REQUIRE(output.playback_rate == Catch::Approx(1.0f));
  REQUIRE(output.frame_time == Catch::Approx(0.0f));
  REQUIRE(output.gait_phase == 0);
  REQUIRE_FALSE(output.suggest_switch);

  // The output struct has no position/velocity/state field at all, so by
  // construction it cannot write the player world position.
  static_assert(sizeof(output) >= sizeof(AnimationId) + sizeof(float) + sizeof(float) + sizeof(int) + sizeof(bool));
}

// ---------------------------------------------------------------------------
// M2: motion-driven playback. The achieved motion decides clip time, rate and
// gait phase; the clip's root XY never leaves the animation layer.
// ---------------------------------------------------------------------------
namespace {

// 11 frames, one frame of clip time per tick at rate 1, and a baked root that
// walks 0.18 m over 10 frames => 1.8 m/s of reference speed.
AnimationClip LocomotionClip(uint32_t id, uint32_t frames = 11) {
  AnimationClip clip;
  clip.id = id;
  clip.frame_count = frames;
  clip.poses.resize(frames);
  clip.root_positions.resize(frames);
  for (uint32_t f = 0; f < frames; ++f) {
    clip.root_positions[f] = Vector3(0.0f, -0.018f * static_cast<float>(f), 0.0f);
  }
  return clip;
}

}  // namespace

TEST_CASE("clip reference speed is derived from the baked root trajectory") {
  const AnimationClip clip = LocomotionClip(1);
  REQUIRE(LocomotionClipReferenceSpeed(clip) ==
          Catch::Approx(1.8f).margin(1e-3f));
  // No trajectory => no reference speed (the caller keeps the native cadence).
  AnimationClip empty;
  empty.frame_count = 5;
  REQUIRE(LocomotionClipReferenceSpeed(empty) == Catch::Approx(0.0f));
}

TEST_CASE("playback advances by the achieved/reference speed ratio") {
  const AnimationClip clip = LocomotionClip(1);
  LocomotionPlaybackRequest request;

  // Achieved 3.6 m/s at 1.8 m/s native => rate 2.0 => two clip frames per tick.
  request = AdvanceLocomotionPlayback(request, 1, clip, 3.6f, 0.0f, 0.01f);
  REQUIRE(request.clip_id == 1);
  REQUIRE(request.playback_rate == Catch::Approx(2.0f).margin(1e-4f));
  REQUIRE(request.frame_time == Catch::Approx(2.0f).margin(1e-4f));

  // Halved speed halves the cadence: continuity across ticks, not a re-sample.
  request = AdvanceLocomotionPlayback(request, 1, clip, 0.9f, 0.0f, 0.01f);
  REQUIRE(request.playback_rate == Catch::Approx(0.5f).margin(1e-4f));
  REQUIRE(request.frame_time == Catch::Approx(2.5f).margin(1e-4f));
}

TEST_CASE("gait phase is continuous and wraps inside the clip cycle") {
  const AnimationClip clip = LocomotionClip(1);
  LocomotionPlaybackRequest request;
  float previous_time = -1.0f;
  int wraps = 0;
  for (int tick = 0; tick < 40; ++tick) {
    const LocomotionPlaybackRequest previous = request;
    request = AdvanceLocomotionPlayback(previous, 1, clip, 3.6f, 0.0f, 0.01f);
    // Inside a cycle the time only ever advances; a wrap is a true loop, not a
    // jump to an unrelated phase.
    REQUIRE(request.frame_time >= 0.0f);
    REQUIRE(request.frame_time < 10.0f);
    if (request.looped) {
      ++wraps;
      REQUIRE(request.frame_time < previous.frame_time);
    } else {
      REQUIRE(request.frame_time > previous.frame_time);
    }
    REQUIRE(request.gait_phase ==
            Catch::Approx(request.frame_time / 10.0f).margin(1e-5f));
    previous_time = request.frame_time;
  }
  REQUIRE(previous_time >= 0.0f);
  REQUIRE(wraps > 0);
}

TEST_CASE("a clip change reseeds playback from the matched entry frame") {
  const AnimationClip first = LocomotionClip(1);
  const AnimationClip second = LocomotionClip(2);
  LocomotionPlaybackRequest request;
  for (int tick = 0; tick < 3; ++tick) {
    request = AdvanceLocomotionPlayback(request, 1, first, 1.8f, 0.0f, 0.01f);
  }
  REQUIRE(request.frame_time == Catch::Approx(3.0f).margin(1e-4f));

  const LocomotionPlaybackRequest switched =
      AdvanceLocomotionPlayback(request, 2, second, 1.8f, 4.0f, 0.01f);
  REQUIRE(switched.clip_id == 2);
  REQUIRE(switched.frame_time == Catch::Approx(5.0f).margin(1e-4f));
  REQUIRE_FALSE(switched.looped);
}

TEST_CASE("the playback request carries no world position") {
  const AnimationClip clip = LocomotionClip(1);
  const LocomotionPlaybackRequest request =
      AdvanceLocomotionPlayback(LocomotionPlaybackRequest{}, 1, clip, 3.0f, 0.0f, 0.01f);
  // Determinism: equal inputs give equal requests, bit for bit.
  const LocomotionPlaybackRequest again =
      AdvanceLocomotionPlayback(LocomotionPlaybackRequest{}, 1, clip, 3.0f, 0.0f, 0.01f);
  REQUIRE(request.frame_time == again.frame_time);
  REQUIRE(request.playback_rate == again.playback_rate);
  REQUIRE(request.gait_phase == again.gait_phase);
  REQUIRE(request.clip_id == again.clip_id);
}

// ---------------------------------------------------------------------------
// STRIDE-0/1: cadence, stride coupling and wrap-aware reference distances.
// ---------------------------------------------------------------------------

TEST_CASE("the stride scale absorbs the speed mismatch by a documented share") {
  // 4 m/s against a 6 m/s reference: a third of the speed is missing.
  const float ratio = 4.0f / 6.0f;
  const float scale = LocomotionStrideScaleTarget(4.0f, 6.0f);
  CHECK(scale == Catch::Approx(std::pow(ratio, kLocomotionStrideCoupling))
                     .margin(1e-5f));
  // Both ends of the design are reachable: pure cadence keeps the stride,
  // pure stride keeps the cadence.
  CHECK(scale < 1.0f);
  CHECK(scale > ratio);  // the stride never takes the whole mismatch alone
  // Native speed or no reference leaves the clip alone.
  CHECK(LocomotionStrideScaleTarget(6.0f, 6.0f) == Catch::Approx(1.0f));
  CHECK(LocomotionStrideScaleTarget(6.0f, 0.0f) == Catch::Approx(1.0f));
  // Bounded: an extreme mismatch cannot fold the leg.
  CHECK(LocomotionStrideScaleTarget(0.5f, 12.0f) ==
        Catch::Approx(kLocomotionMinStrideScale));
  CHECK(LocomotionStrideScaleTarget(20.0f, 2.0f) ==
        Catch::Approx(kLocomotionMaxStrideScale));
}

TEST_CASE("the stride scale is smoothed, never a per-tick reaction") {
  float scale = 1.0f;
  const float target = LocomotionStrideScaleTarget(4.0f, 6.0f);
  const float first = SmoothLocomotionStrideScale(scale, target, 0.01f);
  CHECK(first < 1.0f);
  CHECK(first > target);  // it moves toward the target, not onto it
  // Repeated ticks converge monotonically.
  float previous = first;
  for (int tick = 0; tick < 200; ++tick) {
    scale = SmoothLocomotionStrideScale(previous, target, 0.01f);
    CHECK(scale <= previous + 1e-7f);
    CHECK(scale >= target - 1e-7f);
    previous = scale;
  }
  CHECK(previous == Catch::Approx(target).margin(1e-3f));
}

TEST_CASE("a shortened stride raises the cadence by the same factor") {
  // The foot-lock invariant: stride * reference_speed * playback_rate must equal
  // the achieved speed, whatever the stride scale is.
  const AnimationClip clip = LocomotionClip(1);
  const float reference = LocomotionClipReferenceSpeed(clip);
  const float achieved = 0.8f * reference;
  for (float scale : {kLocomotionMinStrideScale, 0.8f, 1.0f}) {
    LocomotionPlaybackRequest request;
    request = AdvanceLocomotionPlayback(request, 1, clip, achieved, 0.0f, 0.01f,
                                       scale);
    REQUIRE(request.playback_rate > 0.0f);
    REQUIRE(request.playback_rate <= kLocomotionMaxPlaybackRate);
    CHECK(scale * reference * request.playback_rate ==
          Catch::Approx(achieved).margin(1e-4f));
    CHECK(request.stride_scale == Catch::Approx(scale));
  }
}

TEST_CASE("the reference distance of a tick is measured over the sampled span") {
  // 11 samples, 0.02 m per frame: the root track advances 0.02 m per frame time.
  std::vector<Vector3> roots;
  for (int frame = 0; frame < 11; ++frame) {
    roots.push_back(Vector3(0, -0.02f * static_cast<float>(frame), 0));
  }
  CHECK(LocomotionRootPositionAt(roots, 0.0f).coords[1] ==
        Catch::Approx(0.0f));
  CHECK(LocomotionRootPositionAt(roots, 2.5f).coords[1] ==
        Catch::Approx(-0.05f).margin(1e-6f));
  CHECK(LocomotionRootPositionAt(roots, 99.0f).coords[1] ==
        Catch::Approx(-0.20f).margin(1e-6f));

  // A normal tick.
  CHECK(LocomotionReferenceDelta(roots, 1.0f, 2.0f).coords[1] ==
        Catch::Approx(-0.02f).margin(1e-6f));
  // A tick that wraps: the reference must not collapse on the loop boundary.
  const Vector3 wrapped = LocomotionReferenceDelta(roots, 10.0f, 0.5f);
  CHECK(wrapped.coords[1] == Catch::Approx(-0.01f).margin(1e-6f));
}

TEST_CASE("a standing Player stops a movement clip but not an idle one") {
  // A gait clip with a real reference speed must not keep running while the
  // Player is stopped: the stance foot would otherwise slide under a body that
  // is not moving.
  const AnimationClip gait = LocomotionClip(1);
  LocomotionPlaybackRequest request;
  request = AdvanceLocomotionPlayback(request, 1, gait, 3.0f, 0.0f, 0.01f);
  const float moving_time = request.frame_time;
  for (int tick = 0; tick < 10; ++tick) {
    request = AdvanceLocomotionPlayback(request, 1, gait, 0.0f, 0.0f, 0.01f);
  }
  CHECK(request.playback_rate == Catch::Approx(0.0f));
  CHECK(request.frame_time == Catch::Approx(moving_time).margin(1e-6f));

  // A clip without a speed reference is an idle/stance cycle: it keeps its own
  // cadence, because the motion gives it nothing to follow.
  AnimationClip idle = LocomotionClip(2);
  idle.root_positions.assign(idle.frame_count, Vector3(0, 0, 0));
  LocomotionPlaybackRequest idle_request;
  for (int tick = 0; tick < 4; ++tick) {
    idle_request = AdvanceLocomotionPlayback(idle_request, 2, idle, 0.0f, 0.0f,
                                             0.01f);
  }
  CHECK(idle_request.playback_rate == Catch::Approx(1.0f));
  CHECK(idle_request.frame_time > 3.0f);
}

TEST_CASE("a stopped movement clip holds its stride instead of slowing to a floor") {
  // No lower rate floor: at walking pace the ramp is continuous from the
  // derived rate down to zero, so the support foot always travels at the
  // achieved speed instead of being dragged by the plant.
  const AnimationClip clip = LocomotionClip(1);
  const float reference = LocomotionClipReferenceSpeed(clip);
  float previous_rate = 1.0f;
  for (float achieved : {reference, 0.8f * reference, 0.4f * reference, 0.15f}) {
    const float stride = LocomotionStrideScaleTarget(achieved, reference);
    LocomotionPlaybackRequest request;
    request = AdvanceLocomotionPlayback(request, 1, clip, achieved, 0.0f, 0.01f,
                                        stride);
    CHECK(request.playback_rate <= previous_rate + 1e-5f);
    CHECK(stride * reference * request.playback_rate ==
          Catch::Approx(achieved).margin(1e-3f));
    previous_rate = request.playback_rate;
  }
  // At walking pace the stride is clamped (never shortened to nothing), so the
  // product still holds and the cadence carries the rest.
  CHECK(previous_rate > 0.0f);
}

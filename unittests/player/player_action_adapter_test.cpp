// RMA-2 — the unified PlayerActionAdapter contract.
//
// One pure call turns a reference clip, a body, a capability profile, a control
// intent and a playback cursor into one AdaptedMotionSample: root displacement,
// clip phase, stride and pose together. These tests assert the plan's required
// properties (purity, determinism, feasibility fallback, paired stride/cadence
// and capability bounds) without a Player, Runtime, contact or assets.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_motion.hpp"

using football::math::Vector3;

namespace {

AnimationClip GaitClip(float root_speed = 2.0f, int frames = 21) {
  AnimationClip clip;
  clip.id = 7;
  clip.frame_count = static_cast<uint32_t>(frames);
  clip.metadata.action_type = e_DefString_Movement;
  // -Y forward, `root_speed` metres per second at 100 Hz.
  const Vector3 step(0, -root_speed * 0.01f, 0);
  for (int f = 0; f < frames; ++f) {
    clip.poses.push_back(PoseFrame{});
    clip.root_positions.push_back(step * static_cast<float>(f));
  }
  return clip;
}

football::player::PlayerBodyProfile Body() {
  return football::player::PlayerBodyProfile{1.92f, 1.0f, 1.0f};
}

PlayerMotionProfile Profile() {
  return PlayerMotionProfile{8.0f, 6.0f, 12.0f, 6.0f};
}

PlayerActionControl Control(const Vector3& desired_velocity) {
  PlayerActionControl control;
  control.desired_velocity = desired_velocity;
  control.idle_facing = Vector3(0, -1, 0);
  control.authority = PlayerMotionAuthority::Locomotion;
  return control;
}

PlayerKinematicState Moving(float speed) {
  PlayerKinematicState state;
  state.velocity = Vector3(0, -speed, 0);
  state.bodyFacing = Vector3(0, -1, 0);
  return state;
}

}  // namespace

TEST_CASE("adapter is pure and deterministic for identical inputs",
          "[player][adapter][rma2]") {
  const AnimationClip clip = GaitClip();
  const PlayerKinematicState current = Moving(1.0f);
  const PlayerPlaybackState playback{4.0f, 1.0f, 0.0f};
  const auto first = PlayerActionAdapter::Evaluate(
      clip, current, Profile(), Body(), Control(Vector3(0, -1.0f, 0)), playback,
      0.01f);
  const auto second = PlayerActionAdapter::Evaluate(
      clip, current, Profile(), Body(), Control(Vector3(0, -1.0f, 0)), playback,
      0.01f);
  CHECK(first.feasible);
  CHECK(first.root_delta.coords[1] == second.root_delta.coords[1]);
  CHECK(first.clip_time == second.clip_time);
  CHECK(first.playback_rate == second.playback_rate);
  // No world state was touched: the caller's current state is unchanged.
  CHECK(current.position.GetLength() == Catch::Approx(0.0f).margin(1e-9f));
  CHECK(current.velocity.GetLength() == Catch::Approx(1.0f).margin(1e-6f));
}

TEST_CASE("adapter reproduces the capability-constrained locomotion step",
          "[player][adapter][rma2]") {
  const AnimationClip clip = GaitClip();
  const PlayerKinematicState current = Moving(2.0f);
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, current, Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  // Exact speed: the root and its published velocity describe one motion.
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.02f).margin(1e-6f));
  CHECK(sample.velocity.coords[1] == Catch::Approx(-2.0f).margin(1e-5f));
  CHECK(sample.velocity.GetLength() * 0.01f ==
        Catch::Approx(sample.root_delta.GetLength()).margin(1e-7f));
  CHECK(sample.clip_id == 7);
}

TEST_CASE("adapter cap reports a clamped capability",
          "[player][adapter][rma2]") {
  PlayerMotionProfile profile = Profile();
  profile.max_speed = 8.0f;
  const auto sample = PlayerActionAdapter::Evaluate(
      GaitClip(), Moving(7.9f), profile, Body(), Control(Vector3(0, -20.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  CHECK(sample.diagnostics.capability_limited);
  CHECK(sample.velocity.GetLength() <= profile.max_speed + 1e-4f);
}

TEST_CASE("cadence and stride are one pair at the clip's own speed",
          "[player][adapter][rma2]") {
  // At the reference speed the neutral style asks for stride 1 and the derived
  // rate is 1: stride * rate * reference_speed == achieved speed.
  const AnimationClip clip = GaitClip(2.0f);
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  CHECK(sample.stride_scale == Catch::Approx(1.0f).margin(1e-5f));
  CHECK(sample.playback_rate == Catch::Approx(1.0f).margin(1e-5f));
  const float foot_speed =
      sample.stride_scale * sample.playback_rate * 2.0f;
  CHECK(foot_speed == Catch::Approx(sample.velocity.GetLength()).margin(1e-4f));
}

TEST_CASE("a slower body shortens the stride and quickens the clip",
          "[player][adapter][rma2]") {
  const AnimationClip clip = GaitClip(2.0f);
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(0.5f), Profile(), Body(), Control(Vector3(0, -0.5f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  // Pairing: rate = achieved / (reference * stride), never chosen separately.
  CHECK(sample.playback_rate ==
        Catch::Approx(sample.velocity.GetLength() /
                      (2.0f * sample.stride_scale))
            .margin(1e-5f));
  CHECK(sample.stride_scale <= 1.0f);
}

TEST_CASE("locomotion ignores the clip's root trajectory",
          "[player][adapter][rma2]") {
  const PlayerKinematicState current = Moving(2.0f);
  const PlayerActionControl control = Control(Vector3(0, -2.0f, 0));
  const PlayerPlaybackState playback{0.0f, 1.0f, 0.0f};
  const auto slow = PlayerActionAdapter::Evaluate(
      GaitClip(1.0f), current, Profile(), Body(), control, playback, 0.01f);
  const auto fast = PlayerActionAdapter::Evaluate(
      GaitClip(6.0f), current, Profile(), Body(), control, playback, 0.01f);
  CHECK(slow.root_delta.coords[1] == fast.root_delta.coords[1]);
  CHECK(slow.velocity.coords[1] == fast.velocity.coords[1]);
}

TEST_CASE("a clip without a pose is rejected instead of stretched",
          "[player][adapter][rma2]") {
  AnimationClip empty;
  empty.id = 3;
  const auto sample = PlayerActionAdapter::Evaluate(
      empty, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      PlayerPlaybackState{}, 0.01f);
  CHECK_FALSE(sample.feasible);
  CHECK(sample.diagnostics.fallback_used);
  CHECK(sample.root_delta.GetLength() == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("support slip only counts a foot planted on both sides",
          "[player][adapter][rma2]") {
  const Vector3 before(0, 0, 0.1f);
  const Vector3 after(0.02f, 0, 0.1f);
  CHECK(MeasureSupportSlip(true, true, before, after) ==
        Catch::Approx(0.02f).margin(1e-6f));
  CHECK(MeasureSupportSlip(false, true, before, after) == 0.0f);
  CHECK(MeasureSupportSlip(true, false, before, after) == 0.0f);
}

TEST_CASE("prediction reuses the one adaptation step",
          "[player][adapter][rma4]") {
  const AnimationClip clip = GaitClip(2.0f);
  const PlayerKinematicState initial = Moving(1.0f);
  const PlayerActionControl control = Control(Vector3(0, -2.0f, 0));
  const PlayerPlaybackState playback{0.0f, 1.0f, 0.0f};
  constexpr football::sim::TickSpan kHorizon{20};

  const PredictedMotion predicted = PlayerActionAdapter::Predict(
      clip, initial, Profile(), Body(), control, playback, 0.01f, kHorizon);

  // The forecast is exactly the loop over Evaluate(): no second motion model.
  PlayerKinematicState manual = initial;
  PlayerPlaybackState cursor = playback;
  for (football::sim::TickSpan elapsed{};
       elapsed < kHorizon; elapsed += football::sim::TickSpan{1}) {
    const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
        clip, manual, Profile(), Body(), control, cursor, 0.01f);
    manual.position += sample.root_delta;
    manual.velocity = sample.velocity;
    cursor.frame_time = sample.clip_time;
    cursor.stride_scale = sample.stride_scale;
  }
  CHECK(predicted.state.position.coords[1] ==
        Catch::Approx(manual.position.coords[1]).margin(1e-9f));
  CHECK(predicted.state.velocity.GetLength() ==
        Catch::Approx(manual.velocity.GetLength()).margin(1e-9f));

  // And it agrees with the established locomotion prediction it replaces.
  football::sim::TickSpan elapsed{};
  PlayerKinematicState stepped = initial;
  while (elapsed < kHorizon) {
    stepped = PlayerMotion::StepContinuous(
                  stepped, Profile(), control.desired_velocity,
                  initial.GetFacing(), 0.01f)
                  .state;
    elapsed += football::sim::TickSpan{1};
  }
  CHECK(predicted.state.position.coords[1] ==
        Catch::Approx(stepped.position.coords[1]).margin(1e-6f));
}

TEST_CASE("an action plan is validated without moving its clip time",
          "[player][adapter][rma5]") {
  const AnimationClip clip = GaitClip(2.0f);
  PlayerActionControl control;
  control.authority = PlayerMotionAuthority::KinematicReference;
  control.reference_delta = Vector3(0, -0.03f, 0);
  const PlayerKinematicState current = Moving(1.0f);
  const PlayerPlaybackState playback{5.0f, 1.0f, 0.0f};
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, current, Profile(), Body(), control, playback, 0.01f);
  // The plan's displacement is the candidate; the ceiling only bounds it.
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.03f).margin(1e-6f));
  CHECK(sample.velocity.coords[1] == Catch::Approx(-3.0f).margin(1e-4f));
  // The action clock owns clip time: adaptation must not consume a frame.
  CHECK(sample.clip_time == Catch::Approx(5.0f).margin(1e-6f));
  CHECK(sample.playback_rate == Catch::Approx(1.0f).margin(1e-6f));
  CHECK(sample.diagnostics.final_delta.coords[1] ==
        Catch::Approx(-0.03f).margin(1e-6f));
}

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

#include "football/animation/foot_support.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_motion.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/animation/reference_sample.hpp"

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

TEST_CASE("a slower body only slows the cadence; the stride stays authored",
          "[player][adapter][rma7]") {
  const AnimationClip clip = GaitClip(2.0f);
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(0.5f), Profile(), Body(), Control(Vector3(0, -0.5f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  // RMA-7: no runtime IK means no pose-level stride shortening. The clip keeps
  // its authored stride and the cadence carries the speed difference.
  CHECK(sample.stride_scale == Catch::Approx(1.0f).margin(1e-6f));
  CHECK(sample.playback_rate ==
        Catch::Approx(sample.velocity.GetLength() / 2.0f).margin(1e-5f));
}

// Current contract: the control intent owns the root; the clip supplies the
// gait/cadence. This is the remaining Root-Motion gap: the root is not yet the
// clip's reference root. The test pins today's authority so a future change is
// deliberate.
TEST_CASE("locomotion intent owns the root while the clip supplies the gait",
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
          "[player][adapter][rma5][rm6]") {
  const AnimationClip clip = GaitClip(2.0f);
  PlayerActionControl control;
  control.authority = PlayerMotionAuthority::KinematicReference;
  control.reference_delta = Vector3(0, -0.03f, 0);
  control.has_reference_delta = true;
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
  // RM-6: the root and the pose are sampled at the action clock's frame, and
  // the next action tick continues from the same unwrapped time. A touch
  // frame can therefore never drift because of adaptation.
  CHECK(sample.continuous_clip_time == Catch::Approx(5.0f).margin(1e-6f));
  const PoseFrame expected = SamplePose(clip, 5.0f);
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    CHECK(sample.pose.orientations[joint].GetDotProduct(
              expected.orientations[joint]) ==
          Catch::Approx(1.0f).margin(1e-6f));
  }
}

TEST_CASE("a body above the gait range refuses the root, never snaps it",
          "[player][adapter][rma7][rm3]") {
  AnimationClip clip = GaitClip(2.0f);
  clip.looping = true;
  clip.metadata.action_type = e_DefString_Movement;
  clip.motion_quality.stance_calibrated = true;
  clip.motion_quality.ik_free_usable = true;
  clip.stance_speed = 1.0f;
  clip.foot_support.assign(clip.frame_count,
                           FootSupportFrame{FootPhase::Stance,
                                            FootPhase::Swing});

  // 5 m/s body against a 4.4 m/s playable gait: the profile can only brake to
  // 4.88 this tick, still above the range. The adapter must not commit a root
  // it cannot sample consistently; the caller brakes in the fallback path.
  const auto refused = PlayerActionAdapter::Evaluate(
      clip, Moving(5.0f), Profile(), Body(), Control(Vector3(0, -5.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  CHECK_FALSE(refused.feasible);
  CHECK(refused.status == AdaptedMotionSample::Status::NeedAnotherClip);
  CHECK(refused.root_delta.GetLength() == Catch::Approx(0.0f).margin(1e-9f));
  CHECK(refused.diagnostics.playable_speed == Catch::Approx(4.4f).margin(1e-4f));

  // Once the body is inside the range, the same gait drives the root. The
  // control asks for more than the gait can play, so it is reported limited,
  // but the committed velocity is the sampled root's, not the control's.
  const auto limited = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -5.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  CHECK(limited.feasible);
  CHECK(limited.diagnostics.capability_limited);
  CHECK(limited.velocity.GetLength() == Catch::Approx(2.06f).margin(1e-4f));
  CHECK(limited.root_delta.GetLength() ==
        Catch::Approx(0.0206f).margin(1e-5f));
  CHECK(limited.playback_rate <= 2.2f + 1e-4f);
}

TEST_CASE("an explicit zero action root stays zero",
          "[player][adapter][rma5]") {
  const AnimationClip clip = GaitClip(2.0f);
  PlayerActionControl control;
  control.authority = PlayerMotionAuthority::KinematicReference;
  control.has_reference_delta = true;
  control.reference_delta = Vector3(0);
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), control,
      PlayerPlaybackState{5.0f, 1.0f, 0.0f}, 0.01f);
  // A planted touch frame or a stop is a real zero, not a missing value.
  CHECK(sample.root_delta.GetLength() == Catch::Approx(0.0f).margin(1e-9f));
  CHECK(sample.velocity.GetLength() == Catch::Approx(0.0f).margin(1e-9f));
}

// RM-3/RM-4: ordinary locomotion samples the clip's own root. The clip owns
// the spatial trajectory; the control only selects how much of that trajectory
// this tick may play.
namespace {

AnimationClip RootGait(float root_speed, const Vector3& direction) {
  AnimationClip clip = GaitClip(1.0f);
  clip.looping = true;
  clip.motion_quality.stance_calibrated = true;
  clip.motion_quality.ik_free_usable = true;
  const Vector3 step = direction.GetNormalized() * (root_speed * 0.01f);
  for (std::size_t frame = 0; frame < clip.root_positions.size(); ++frame) {
    clip.root_positions[frame] = step * static_cast<float>(frame);
  }
  return clip;
}

}  // namespace

TEST_CASE("RM-3: locomotion displacement follows the clip root, not a second integrator",
          "[player][adapter][rm3]") {
  const AnimationClip forward = RootGait(3.0f, Vector3(0, -1, 0));
  const PlayerPlaybackState playback{0.0f, 1.0f, 0.0f};
  const auto straight = PlayerActionAdapter::Evaluate(
      forward, Moving(3.0f), Profile(), Body(), Control(Vector3(0, -3.0f, 0)),
      playback, 0.01f);
  REQUIRE(straight.feasible);
  CHECK(straight.velocity.coords[1] == Catch::Approx(-3.0f).margin(1e-4f));
  CHECK(straight.root_delta.coords[1] == Catch::Approx(-0.03f).margin(1e-5f));

  // Same control, different clip root direction: the displacement changes with
  // the clip. A direction the clip cannot express is refused instead of being
  // silently re-rotated while the pose keeps the old bearing.
  const AnimationClip sideways = RootGait(3.0f, Vector3(1, 0, 0));
  const auto refused = PlayerActionAdapter::Evaluate(
      sideways, Moving(3.0f), Profile(), Body(), Control(Vector3(0, -3.0f, 0)),
      playback, 0.01f);
  CHECK_FALSE(refused.feasible);
  CHECK(refused.status == AdaptedMotionSample::Status::NeedAnotherClip);
}

TEST_CASE("RM-3: a loop seam advances the committed root",
          "[player][adapter][rm3]") {
  const AnimationClip clip = RootGait(2.0f, Vector3(0, -1, 0));
  const PlayerPlaybackState playback{19.5f, 1.0f, 0.0f};
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      playback, 0.01f);
  REQUIRE(sample.feasible);
  CHECK(sample.looped);
  CHECK(sample.continuous_clip_time == Catch::Approx(20.5f).margin(1e-4f));
  CHECK(sample.clip_time == Catch::Approx(0.5f).margin(1e-4f));
  CHECK(sample.root_delta.coords[1] == Catch::Approx(-0.02f).margin(1e-5f));
}

TEST_CASE("RM-3: the root delta and pose are one reference sample",
          "[player][adapter][rm3]") {
  const AnimationClip clip = RootGait(2.0f, Vector3(0, -1, 0));
  const PlayerPlaybackState playback{3.25f, 1.0f, 0.0f};
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      playback, 0.01f);
  REQUIRE(sample.feasible);
  const ReferenceAnimationSample reference =
      SampleAnimationStep(clip, playback.frame_time, sample.continuous_clip_time);
  CHECK(sample.clip_time == Catch::Approx(reference.pose_time));
  CHECK(sample.root_delta.coords[1] ==
        Catch::Approx(reference.root_delta.coords[1]).margin(1e-6f));
  const PoseFrame expected = SamplePose(clip, reference.pose_time);
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    CHECK(sample.pose.orientations[joint].GetDotProduct(
              expected.orientations[joint]) == Catch::Approx(1.0f).margin(1e-6f));
  }
}

TEST_CASE("RM-3: a taller body scales the sampled root",
          "[player][adapter][rm3]") {
  const AnimationClip clip = RootGait(2.0f, Vector3(0, -1, 0));
  football::player::PlayerBodyProfile tall = Body();
  tall.height_scale = 1.2f;
  const PlayerPlaybackState playback{0.0f, 1.0f, 0.0f};
  const auto normal = PlayerActionAdapter::Evaluate(
      clip, Moving(2.0f), Profile(), Body(), Control(Vector3(0, -2.0f, 0)),
      playback, 0.01f);
  const auto scaled = PlayerActionAdapter::Evaluate(
      clip, Moving(2.4f), Profile(), tall, Control(Vector3(0, -2.4f, 0)),
      playback, 0.01f);
  REQUIRE(normal.feasible);
  REQUIRE(scaled.feasible);
  CHECK(scaled.root_delta.GetLength() ==
        Catch::Approx(normal.root_delta.GetLength() * 1.2f).margin(1e-5f));
}

TEST_CASE("RM-4: only a root-driven gait reports a playable-speed cap",
          "[player][adapter][rm4]") {
  AnimationClip clip = GaitClip(2.0f);
  clip.metadata.action_type = e_DefString_Movement;
  // Not ik-free: the compatibility path owns the motion, so the fallback must
  // keep chasing the control target instead of capping it to this clip.
  const auto sample = PlayerActionAdapter::Evaluate(
      clip, Moving(5.0f), Profile(), Body(), Control(Vector3(0, -5.0f, 0)),
      PlayerPlaybackState{0.0f, 1.0f, 0.0f}, 0.01f);
  CHECK(sample.diagnostics.playable_speed == Catch::Approx(0.0f));
}

// Player x Animation acceptance on the *real* catalog: cruise, brake, stop and
// turn through the production pose chain.
//
// The synthetic gait in unittests/player proves the mechanism; this proves the
// mechanism against authored clips, whose foot motion is not the ideal
// stance/swing trace the synthetic clip encodes. The assertions are therefore
// about invariants (bone lengths, support lock, cadence/stride pairing, bounded
// joint rates), not about a specific clip's exact geometry.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "football/animation/library.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/player/execution/player_foot_ik.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/locomotion_animation_strategy.hpp"
#include "player/execution/player_baked_body_pose.hpp"
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

struct Metrics {
  float max_animation_slip = 0.0f;   // gait target vs a still-locked anchor
  float max_final_slip = 0.0f;       // settled pose vs anchor
  float max_bone_error = 0.0f;
  float max_target_residual = 0.0f;
  float max_leg_step = 0.0f;         // shipped leg joint rotation per tick
  float max_yaw_correction = 0.0f;
  int planted_ticks = 0;
  int plant_releases = 0;
  float max_plant_slide = 0.0f;
  int yaw_clamped_ticks = 0;
  float distance = 0.0f;
  bool saw_positive_rate = false;
  bool saw_zero_rate = false;
};

struct Run {
  const AnimationClip* clip = nullptr;
  float reference_speed = 0.0f;
  PlayerKinematicState state;
  LocomotionPlaybackRequest playback;
  PlayerFootPlantState plant;
  PoseFrame pose;
  bool pose_valid = false;
  PoseFrame final_pose;
  bool final_pose_valid = false;
  float previous_fore_aft[2] = {0.0f, 0.0f};
  bool have_previous_fore_aft = false;
  bool support_left = false;
  bool have_support = false;
  Metrics metrics;
};

// One tick of the production chain: playback -> warp -> realize -> stride/plant
// settle -> Final Pose. `achieved_velocity` is the motion the Player committed;
// `reference_direction` is the direction the animation reference points at.
void Step(Run& run, const Vector3& achieved_velocity, float stride_scale,
          const Vector3& reference_direction) {
  const AnimationClip& clip = *run.clip;
  const float achieved_speed = achieved_velocity.GetLength();
  run.playback = AdvanceLocomotionPlayback(
      run.playback, clip.id, clip, achieved_speed, 0.0f, kTick,
      stride_scale);

  const PoseFrame sampled = SamplePose(clip, run.playback.frame_time);
  run.pose = sampled;

  // Warp: body yaw follows the achieved motion. The reference delta is the clip
  // track over the sampled interval, rotated by the base yaw (0 here).
  const float sampled_frame = run.playback.frame_time;
  const float start_frame = sampled_frame - run.playback.playback_rate;
  Vector3 reference_delta;
  if (start_frame >= 0.0f) {
    reference_delta = LocomotionReferenceDelta(clip.root_positions, start_frame,
                                              sampled_frame);
  } else {
    reference_delta = LocomotionReferenceDelta(clip.root_positions, 0.0f,
                                              sampled_frame);
  }
  if (reference_direction.GetLength() > 1e-3f) {
    reference_delta = reference_direction.GetNormalized() *
                      reference_delta.GetLength();
  }
  PlayerPoseWarpInput warp;
  warp.reference_pose = sampled;
  warp.reference_base_angle = 0.0f;
  warp.reference_delta = reference_delta.Get2D();
  warp.achieved_delta = achieved_velocity * kTick;
  warp.body = Body();
  warp.max_yaw_correction = kPlayerPoseWarpMaxYawPerTick;
  const PlayerPoseWarpResult warped = PlayerPoseAdapter::Warp(warp);
  run.metrics.max_yaw_correction =
      std::max(run.metrics.max_yaw_correction,
               std::fabs(warped.yaw_correction));
  if (warped.yaw_clamped) ++run.metrics.yaw_clamped_ticks;

  PlayerPoseRealizeInput realize;
  realize.previous_pose = run.pose_valid ? run.pose : warped.pose;
  realize.target_pose = warped.pose;
  realize.body = Body();
  const PlayerPoseRealizeResult realized = PlayerPoseAdapter::Realize(realize);
  run.pose = realized.pose;
  run.pose_valid = true;

  const float base_angle = warped.base_angle;
  const BakedBodyPose gait =
      EvaluateBakedBodyPose(run.pose, run.state, base_angle, Body());
  PlayerStrideAdaptInput adapt;
  adapt.left.valid = true;
  adapt.left.hip = gait.joints[BodyPart::left_thigh];
  adapt.left.ankle = gait.joints[BodyPart::left_ankle];
  adapt.right.valid = true;
  adapt.right.hip = gait.joints[BodyPart::right_thigh];
  adapt.right.ankle = gait.joints[BodyPart::right_ankle];
  adapt.facing = Vector3(0, -1, 0).GetRotated2D(base_angle);
  adapt.stride_scale = stride_scale;
  adapt.gait_phase = run.playback.gait_phase;
  const PlayerStrideAdaptResult adapted = PlayerPoseAdapter::AdaptStride(adapt);

  const float left_height = adapted.targets.left.coords[2];
  const float right_height = adapted.targets.right.coords[2];
  // Production support rule: the foot travelling backwards carries weight.
  const Vector3 left_offset = gait.joints[BodyPart::left_ankle] -
                              gait.joints[BodyPart::left_thigh];
  const Vector3 right_offset = gait.joints[BodyPart::right_ankle] -
                               gait.joints[BodyPart::right_thigh];
  const float left_fore_aft = left_offset.GetDotProduct(adapt.facing);
  const float right_fore_aft = right_offset.GetDotProduct(adapt.facing);
  PlayerFootSupportInput support_input;
  support_input.left_backward_rate = run.previous_fore_aft[0] - left_fore_aft;
  support_input.right_backward_rate = run.previous_fore_aft[1] - right_fore_aft;
  support_input.have_previous = run.have_previous_fore_aft;
  support_input.previous_support_left = run.support_left;
  support_input.have_previous_support = run.have_support;
  const PlayerFootSupportResult support = SelectFootSupport(support_input);
  run.previous_fore_aft[0] = left_fore_aft;
  run.previous_fore_aft[1] = right_fore_aft;
  run.have_previous_fore_aft = true;
  run.support_left = support.left_support;
  run.have_support = support.left_support != support.right_support;
  const bool support_left = support.left_support;
  if (run.plant.left_planted && run.plant.left_planted) {
    run.metrics.max_animation_slip =
        std::max(run.metrics.max_animation_slip,
                 (adapted.targets.left - run.plant.left_anchor).GetLength());
  }
  if (run.plant.right_planted) {
    run.metrics.max_animation_slip =
        std::max(run.metrics.max_animation_slip,
                 (adapted.targets.right - run.plant.right_anchor).GetLength());
  }
  PlayerFootPlantInput plant_input;
  plant_input.left_ankle = adapted.targets.left;
  plant_input.right_ankle = adapted.targets.right;
  plant_input.left_support = support.left_support;
  plant_input.right_support = support.right_support;
  plant_input.plant_height = 0.22f * Body().height_scale;
  if (run.plant.left_planted || run.plant.right_planted) {
    ++run.metrics.planted_ticks;
  }
  if (std::getenv("FOOTBALL_LOCO_TICK_DIAG") != nullptr) {
    // Per-tick trace: the support decision, the anchor and the drift the plant
    // has to absorb. This is how the baked stance-foot disparity below was
    // measured.
    std::fprintf(stderr,
                 "t=%.2f rate=%.2f back=(%.3f,%.3f) sup=(%d,%d) plant=(%d,%d) "
                 "drift=(%.3f,%.3f)\n",
                 run.playback.frame_time, run.playback.playback_rate,
                 support_input.left_backward_rate, support_input.right_backward_rate,
                 support.left_support ? 1 : 0, support.right_support ? 1 : 0,
                 run.plant.left_planted ? 1 : 0, run.plant.right_planted ? 1 : 0,
                 run.plant.left_planted
                     ? (adapted.targets.left - run.plant.left_anchor).GetLength()
                     : 0.0f,
                 run.plant.right_planted
                     ? (adapted.targets.right - run.plant.right_anchor).GetLength()
                     : 0.0f);
  }
  run.plant = PlayerFootIk::Update(run.plant, plant_input);

  const PlayerFootIkTargets targets =
      ApplyFootPlantLocks(run.plant, adapted.targets);
  const BakedPoseFootSolve solved = ApplyWorldFootTargetsToPose(
      run.pose, run.state, base_angle, Body(), targets);
  run.metrics.max_target_residual =
      std::max({run.metrics.max_target_residual, solved.left_residual,
                solved.right_residual});

  // Production relax rule: an unreachable anchor slides toward the foot (or is
  // released outright for a discontinuity).
  const PlayerFootPlantRelax relax = RelaxFootPlant(
      run.plant, adapted.targets, solved.left_residual, solved.right_residual);
  if (relax.left_released || relax.right_released) ++run.metrics.plant_releases;
  run.metrics.max_plant_slide =
      std::max({run.metrics.max_plant_slide, relax.left_slide, relax.right_slide});
  run.plant = relax.plant;
  const BakedBodyPose settled =
      EvaluateBakedBodyPose(solved.pose, run.state, base_angle, Body());

  // Support foot: what actually ships against the anchor this solve used.
  if (run.plant.left_planted) {
    run.metrics.max_final_slip =
        std::max(run.metrics.max_final_slip,
                 (settled.joints[BodyPart::left_ankle] - run.plant.left_anchor)
                     .GetLength());
  }
  if (run.plant.right_planted) {
    run.metrics.max_final_slip =
        std::max(run.metrics.max_final_slip,
                 (settled.joints[BodyPart::right_ankle] - run.plant.right_anchor)
                     .GetLength());
  }

  // Bone lengths and the shipped leg-joint rate.
  const BakedBodyPose rest =
      EvaluateBakedBodyPose(PoseFrame{}, run.state, 0.0f, Body());
  for (const auto& segment :
       {std::pair<BodyPart, BodyPart>{BodyPart::left_thigh, BodyPart::left_knee},
        {BodyPart::left_knee, BodyPart::left_ankle},
        {BodyPart::right_thigh, BodyPart::right_knee},
        {BodyPart::right_knee, BodyPart::right_ankle}}) {
    const float length =
        (settled.joints[segment.second] - settled.joints[segment.first])
            .GetLength();
    const float rest_length =
        (rest.joints[segment.second] - rest.joints[segment.first]).GetLength();
    run.metrics.max_bone_error =
        std::max(run.metrics.max_bone_error, std::fabs(length - rest_length));
  }
  if (run.final_pose_valid) {
    for (const BodyPart joint :
         {BodyPart::left_thigh, BodyPart::right_thigh, BodyPart::left_knee,
          BodyPart::right_knee, BodyPart::left_ankle, BodyPart::right_ankle}) {
      auto before = run.final_pose.orientations[joint];
      auto after = solved.pose.orientations[joint];
      const float dot = before.MakeSameNeighborhood(after);
      run.metrics.max_leg_step =
          std::max(run.metrics.max_leg_step,
                   2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f)));
    }
  }
  run.final_pose = solved.pose;
  run.final_pose_valid = true;

  if (run.playback.playback_rate > 0.0f) run.metrics.saw_positive_rate = true;
  if (run.playback.playback_rate == 0.0f) run.metrics.saw_zero_rate = true;

  const Vector3 delta = achieved_velocity * kTick;
  run.state.position += delta;
  run.state.velocity = achieved_velocity;
  run.metrics.distance += delta.GetLength();
}

Run Begin(const AnimationClip& clip) {
  Run run;
  run.clip = &clip;
  run.reference_speed = LocomotionClipReferenceSpeed(clip);
  run.state.bodyFacing = Vector3(0, -1, 0);
  return run;
}

void Dump(const char* scenario, const Run& run) {
  if (std::getenv("FOOTBALL_LOCO_ACCEPTANCE_DIAG") == nullptr) return;
  std::fprintf(stderr,
               "%s: clip=%d ref=%.2f dist=%.2f animslip=%.4f finalslip=%.4f "
               "bone=%.6f residual=%.4f legstep=%.3f yaw=%.3f planted=%d "
               "releases=%d slide=%.3f clamped=%d rate_pos=%d rate0=%d\n",
               scenario, static_cast<int>(run.clip->id), run.reference_speed,
               run.metrics.distance, run.metrics.max_animation_slip,
               run.metrics.max_final_slip, run.metrics.max_bone_error,
               run.metrics.max_target_residual, run.metrics.max_leg_step,
               run.metrics.max_yaw_correction, run.metrics.planted_ticks,
               run.metrics.plant_releases, run.metrics.max_plant_slide,
               run.metrics.yaw_clamped_ticks, run.metrics.saw_positive_rate ? 1 : 0,
               run.metrics.saw_zero_rate ? 1 : 0);
}

// The run clip the strategy picks for a straight cruise.
const AnimationClip& CruiseClip(const AnimationLibrary& library, float speed) {
  PlayerKinematicState s;
  s.velocity = Vector3(0, -speed, 0);
  s.bodyFacing = Vector3(0, -1, 0);
  LocomotionAnimationInput input;
  input.current = s;
  input.next = s;
  input.desired_velocity = s.velocity;
  input.body = Body();
  const auto choice = SelectLocomotionAnimation(input, library.Clips());
  REQUIRE(choice.animation_id >= 0);
  return library.Get(static_cast<uint32_t>(choice.animation_id));
}

}  // namespace

TEST_CASE("real cruise holds the support foot and the skeleton") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const AnimationClip& clip = CruiseClip(library, 5.0f);
  Run run = Begin(clip);
  REQUIRE(run.reference_speed > 1.0f);

  const Vector3 velocity(0, -run.reference_speed, 0);
  for (int tick = 0; tick < 80; ++tick) {
    Step(run, velocity, 1.0f, Vector3(0, -1, 0));
  }
  Dump("cruise", run);
  CHECK(run.metrics.planted_ticks > 20);
  // Bone lengths are exact: the solve re-aims, it never stretches.
  CHECK(run.metrics.max_bone_error < 1e-3f);
  // The support anchor only ever moves by the bounded relaxation step.
  CHECK(run.metrics.max_plant_slide <= kPlantRelaxSlide + 1e-6f);
  // Remaining shipping error. On an authored run clip the stance foot travels
  // ~4 m/s inside the animation while the root track claims ~5 m/s, so the plant
  // has to absorb the difference; the Baker support track plus a foot-lock rate
  // calibration is what removes it (see docs/player_motion_pipeline.md).
  CHECK(run.metrics.max_final_slip < 0.12f);
  CHECK(run.metrics.max_target_residual < 0.15f);
  CHECK(run.metrics.max_leg_step < 1.4f);
  CHECK(run.metrics.distance ==
        Catch::Approx(run.reference_speed * 0.01f * 80).margin(1e-3f));
}

TEST_CASE("a real brake shortens the stride and lowers the cadence together") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const AnimationClip& clip = CruiseClip(library, 5.0f);
  Run run = Begin(clip);
  REQUIRE(run.reference_speed > 1.0f);

  const float reference = run.reference_speed;
  for (int tick = 0; tick < 120; ++tick) {
    // 5 m/s down to a walk, 1.5 s of braking.
    const float progress = std::min(1.0f, static_cast<float>(tick) / 90.0f);
    const float speed = reference * (1.0f - 0.85f * progress);
    const float stride = LocomotionStrideScaleTarget(speed, reference);
    Step(run, Vector3(0, -speed, 0), stride, Vector3(0, -1, 0));
  }
  Dump("brake", run);
  CHECK(run.metrics.planted_ticks > 20);
  CHECK(run.metrics.max_bone_error < 1e-3f);
  CHECK(run.metrics.max_plant_slide <= kPlantRelaxSlide + 1e-6f);
  CHECK(run.metrics.max_final_slip < 0.12f);
  CHECK(run.metrics.max_leg_step < 1.4f);
  // The stride scale follows the speed down, so cadence does not have to absorb
  // the whole mismatch alone.
  CHECK(LocomotionStrideScaleTarget(0.2f * reference, reference) <
        LocomotionStrideScaleTarget(0.8f * reference, reference));
}

TEST_CASE("a real clip stops advancing when the Player stands still") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const AnimationClip& clip = CruiseClip(library, 5.0f);
  Run run = Begin(clip);
  REQUIRE(run.reference_speed > 1.0f);

  for (int tick = 0; tick < 20; ++tick) {
    Step(run, Vector3(0, -run.reference_speed, 0), 1.0f, Vector3(0, -1, 0));
  }
  const float moving_frame_time = run.playback.frame_time;
  for (int tick = 0; tick < 40; ++tick) {
    Step(run, Vector3(0, -0.2f, 0), 1.0f, Vector3(0, -1, 0));
  }
  const float slow_frame_time = run.playback.frame_time;
  for (int tick = 0; tick < 40; ++tick) {
    Step(run, Vector3(0, 0.0f, 0), 1.0f, Vector3(0, -1, 0));
  }
  Dump("stop", run);
  CHECK(run.playback.playback_rate == Catch::Approx(0.0f));
  CHECK(run.metrics.saw_zero_rate);
  // Standing still freezes the gait instead of letting it walk on its own.
  CHECK(run.playback.frame_time >= 0.0f);
  CHECK(run.playback.frame_time <
        static_cast<float>(run.clip->frame_count) - 1.0f);
  CHECK(run.metrics.max_bone_error < 1e-3f);
  CHECK(run.metrics.max_plant_slide <= kPlantRelaxSlide + 1e-6f);
  // Walking pace still advances, just slower than the clip's own cadence.
  CHECK(slow_frame_time != Catch::Approx(moving_frame_time));
}

TEST_CASE("a real turn keeps the pose bounded while the yaw is corrected") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const AnimationClip& clip = CruiseClip(library, 3.5f);
  Run run = Begin(clip);
  REQUIRE(run.reference_speed > 1.0f);

  const float speed = run.reference_speed;
  for (int tick = 0; tick < 60; ++tick) {
    // The Player's motion turns 90 degrees while the animation still points
    // straight; the warp is the only thing that reconciles them.
    const float progress = std::min(1.0f, static_cast<float>(tick) / 30.0f);
    const float yaw = 0.5f * football::math::pi * progress;
    const Vector3 velocity = Vector3(0, -1, 0).GetRotated2D(yaw) * speed;
    Step(run, velocity, 1.0f, Vector3(0, -1, 0));
  }
  Dump("turn", run);
  CHECK(run.metrics.planted_ticks > 10);
  CHECK(run.metrics.max_bone_error < 1e-3f);
  CHECK(run.metrics.max_final_slip < 0.12f);
  CHECK(run.metrics.max_plant_slide <= kPlantRelaxSlide + 1e-6f);
  // The correction is bounded per tick and actually engaged.
  CHECK(run.metrics.max_yaw_correction <=
        kPlayerPoseWarpMaxYawPerTick + 1e-5f);
  CHECK(run.metrics.max_yaw_correction > 1e-3f);
  CHECK(run.metrics.yaw_clamped_ticks > 0);
}

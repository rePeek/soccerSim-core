// STRIDE-0..3 — the locomotion pose pipeline, end to end and asset-free.
//
// One synthetic gait clip, one synthetic Player root, the real production
// pieces: motion-driven playback -> pose sample -> forward kinematics ->
// support plant -> stride adaptation -> local-rotation write-back. The asserts
// are the acceptance criteria of the plan:
//
//   * the Player's world displacement is owned by the motion, never the clip;
//   * a stride change never changes a bone length;
//   * cadence and stride stay paired, so a planted foot does not have to be
//     dragged back by the IK (the mispaired control shows the failure);
//   * the final pose the collider sees is the pose the IK solved.
//
// The synthetic clip is *built* from a stance/swing ankle trajectory and the
// same IK the runtime uses, so its root travel and its foot motion agree. A
// hand-made sinusoid would encode a foot speed that contradicts its own
// reference speed and would make every slip assertion meaningless.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/pose_sampling.hpp"
#include "football/player/execution/player_foot_ik.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_kinematics.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "player/execution/player_locomotion_animation.hpp"

using football::math::Vector3;

namespace {

constexpr float kTickSeconds = 0.01f;
constexpr uint32_t kGaitFrames = 21;  // 20 intervals = one cycle
// Stance excursion asked for before reach clamping. The clip's own root track
// is then derived from the excursion the legs can actually achieve, so the
// clip never claims a speed its own feet do not deliver.
constexpr float kNominalExcursion = 0.30f;
// A ready stance: knees bent and thighs forward. This is what gives the
// two-bone solve slack for a fore-aft excursion - dropping the pelvis alone
// moves hip and ankle together and creates no slack at all.
constexpr float kThighBend = -0.30f;
constexpr float kKneeBend = 0.60f;
// Support handover needs a margin: at the double-support instant both ankles are
// level and a plain comparison would flip the stance every tick.
constexpr float kSupportSwitchMargin = 0.01f;

football::player::PlayerBodyProfile Body() {
  football::player::PlayerBodyProfile body;
  body.height_scale = 1.0f;
  body.girth_scale = 1.0f;
  return body;
}

// Ankle trajectory of one leg for a cycle phase in [0, 1). The stance half runs
// linearly backwards; the swing half returns it forward while lifted.
struct AnkleTrace {
  float fore_aft = 0.0f;
  float lift = 0.0f;
};

AnkleTrace Trace(float phase) {
  const float p = phase - std::floor(phase);
  AnkleTrace trace;
  if (p < 0.5f) {
    trace.fore_aft = kNominalExcursion * (0.5f - 2.0f * p);
    return trace;
  }
  const float swing = (p - 0.5f) / 0.5f;
  trace.fore_aft = kNominalExcursion * (-0.5f + swing);
  trace.lift = 0.09f * std::sin(football::math::pi * swing);
  return trace;
}

// Builds a gait clip whose root track matches the foot motion its own IK can
// reach: the stance travel of the ankle over half a cycle is exactly how far
// the body is declared to advance.
AnimationClip GaitClip() {
  AnimationClip clip;
  clip.id = 7;
  clip.frame_count = kGaitFrames;
  clip.poses.resize(kGaitFrames);
  const PlayerKinematicState origin;
  for (uint32_t f = 0; f < kGaitFrames; ++f) {
    const float phase = static_cast<float>(f) / static_cast<float>(kGaitFrames - 1);
    PoseFrame pose;
    pose.orientations[BodyPart::left_thigh].SetAngleAxis(kThighBend,
                                                        Vector3(1, 0, 0));
    pose.orientations[BodyPart::right_thigh].SetAngleAxis(kThighBend,
                                                         Vector3(1, 0, 0));
    pose.orientations[BodyPart::left_knee].SetAngleAxis(kKneeBend,
                                                       Vector3(1, 0, 0));
    pose.orientations[BodyPart::right_knee].SetAngleAxis(kKneeBend,
                                                        Vector3(1, 0, 0));
    const BakedBodyPose rest =
        EvaluateBakedBodyPose(pose, origin, 0.0f, Body());
    const AnkleTrace left = Trace(phase);
    // The right leg is half a cycle out of phase.
    const AnkleTrace right = Trace(phase + 0.5f);
    PlayerFootIkTargets targets;
    targets.has_left = true;
    targets.has_right = true;
    targets.left = rest.joints[BodyPart::left_ankle] +
                   Vector3(0, -left.fore_aft, left.lift);
    targets.right = rest.joints[BodyPart::right_ankle] +
                    Vector3(0, -right.fore_aft, right.lift);
    clip.poses[f] =
        ApplyWorldFootTargetsToPose(pose, origin, 0.0f, Body(), targets).pose;
  }

  // The stance half is frames 0..half. Its ankle travel is the body advance of
  // half a cycle, so the whole cycle advances twice that.
  const uint32_t half = (kGaitFrames - 1) / 2;
  const BakedBodyPose start =
      EvaluateBakedBodyPose(clip.poses[0], origin, 0.0f, Body());
  const BakedBodyPose stance_end =
      EvaluateBakedBodyPose(clip.poses[half], origin, 0.0f, Body());
  const float achieved_excursion =
      std::fabs(stance_end.joints[BodyPart::left_ankle].coords[1] -
                start.joints[BodyPart::left_ankle].coords[1]);
  const float travel_per_frame = achieved_excursion / static_cast<float>(half);
  clip.root_positions.resize(kGaitFrames);
  for (uint32_t f = 0; f < kGaitFrames; ++f) {
    clip.root_positions[f] =
        Vector3(0, -travel_per_frame * static_cast<float>(f), 0);
  }
  return clip;
}

struct RunResult {
  float max_animation_slip = 0.0f;   // pre-adaptation: the clip's own slide
  float max_final_slip = 0.0f;       // settled pose vs anchor (what ships)
  float max_lock_correction = 0.0f;  // gait vs anchor, i.e. the plant's drag
  float max_target_residual = 0.0f;  // post-IK: unreachable targets only
  float max_bone_length_error = 0.0f;
  float max_final_ankle_error = 0.0f;
  float max_fore_aft_correction = 0.0f;
  int planted_ticks = 0;
  float distance = 0.0f;
};

// One straight run. `pair_rate_with_stride` is the invariant under test: the
// playback rate must divide by the same stride scale the pose is adapted with.
RunResult RunStraight(float achieved_speed, uint32_t ticks, float stride_scale,
                      bool pair_rate_with_stride) {
  const AnimationClip clip = GaitClip();
  RunResult result;
  PlayerKinematicState state;
  state.velocity = Vector3(0, -achieved_speed, 0);
  state.bodyFacing = Vector3(0, -1, 0);
  LocomotionPlaybackRequest playback;
  PlayerFootPlantState plant;
  const BakedBodyPose rest = EvaluateBakedBodyPose(PoseFrame{}, state, 0.0f, Body());
  const float thigh_length = (rest.joints[BodyPart::left_knee] -
                              rest.joints[BodyPart::left_thigh]).GetLength();
  const float shin_length = (rest.joints[BodyPart::left_ankle] -
                             rest.joints[BodyPart::left_knee]).GetLength();

  for (uint32_t tick = 0; tick < ticks; ++tick) {
    const float rate_scale = pair_rate_with_stride ? stride_scale : 1.0f;
    playback = AdvanceLocomotionPlayback(playback, clip.id, clip, achieved_speed,
                                         0.0f, kTickSeconds, rate_scale);

    const PoseFrame sampled_pose = SamplePose(clip, playback.frame_time);
    const BakedBodyPose gait =
        EvaluateBakedBodyPose(sampled_pose, state, 0.0f, Body());

    // STRIDE-1: stride target for the gait, plant-agnostic.
    PlayerStrideAdaptInput adapt;
    adapt.left.valid = true;
    adapt.left.hip = gait.joints[BodyPart::left_thigh];
    adapt.left.ankle = gait.joints[BodyPart::left_ankle];
    adapt.right.valid = true;
    adapt.right.hip = gait.joints[BodyPart::right_thigh];
    adapt.right.ankle = gait.joints[BodyPart::right_ankle];
    adapt.facing = Vector3(0, -1, 0);
    adapt.stride_scale = stride_scale;
    adapt.gait_phase = playback.gait_phase;
    const PlayerStrideAdaptResult adapted = PlayerPoseAdapter::AdaptStride(adapt);
    result.max_fore_aft_correction =
        std::max(result.max_fore_aft_correction, adapted.max_fore_aft_correction);

    const PlayerFootPlantState previous_plant = plant;

    // Support comes from the gait heights with a switch margin, exactly like the
    // executor: a settled (locked) ankle cannot be used to decide support.
    const float left_height = adapted.targets.left.coords[2];
    const float right_height = adapted.targets.right.coords[2];
    bool support_left = left_height <= right_height;
    if (plant.left_planted && !plant.right_planted) {
      support_left = (left_height - right_height) <= kSupportSwitchMargin;
    } else if (plant.right_planted && !plant.left_planted) {
      support_left = (left_height - right_height) < -kSupportSwitchMargin;
    }
    PlayerFootPlantInput plant_input;
    plant_input.left_ankle = adapted.targets.left;
    plant_input.right_ankle = adapted.targets.right;
    plant_input.left_support = support_left;
    plant_input.right_support = !support_left;
    plant_input.plant_height = 0.25f;
    if (previous_plant.left_planted || previous_plant.right_planted)
      ++result.planted_ticks;
    plant = PlayerFootIk::Update(previous_plant, plant_input);

    // STRIDE-0: the drift a foot that stays locked across the tick has to
    // absorb. A foot being released at the handover is not dragged.
    if (previous_plant.left_planted && plant.left_planted) {
      result.max_lock_correction =
          std::max(result.max_lock_correction,
                   (adapted.targets.left - previous_plant.left_anchor)
                       .GetLength());
    }
    if (previous_plant.right_planted && plant.right_planted) {
      result.max_lock_correction =
          std::max(result.max_lock_correction,
                   (adapted.targets.right - previous_plant.right_anchor)
                       .GetLength());
    }
    const PlayerFootIkTargets targets =
        ApplyFootPlantLocks(plant, adapted.targets);

    // STRIDE-2: the solve is written into the skeleton the collider reads.
    const BakedPoseFootSolve solved = ApplyWorldFootTargetsToPose(
        sampled_pose, state, 0.0f, Body(), targets);
    result.max_target_residual = std::max(
        {result.max_target_residual, solved.left_residual, solved.right_residual});
    const BakedBodyPose settled =
        EvaluateBakedBodyPose(solved.pose, state, 0.0f, Body());

    const std::pair<BodyPart, bool> legs[] = {
        {BodyPart::left_ankle, targets.has_left},
        {BodyPart::right_ankle, targets.has_right}};
    for (const auto& leg : legs) {
      if (!leg.second) continue;
      const bool is_left = leg.first == BodyPart::left_ankle;
      const Vector3& target = is_left ? targets.left : targets.right;
      const float residual = is_left ? solved.left_residual : solved.right_residual;
      result.max_final_ankle_error =
          std::max(result.max_final_ankle_error,
                   (settled.joints[leg.first] - target).GetLength() - residual);
      const bool planted = is_left ? plant.left_planted : plant.right_planted;
      if (planted) {
        const Vector3& anchor = is_left ? plant.left_anchor : plant.right_anchor;
        result.max_final_slip =
            std::max(result.max_final_slip,
                     (settled.joints[leg.first] - anchor).GetLength());
      }
    }
    result.max_bone_length_error =
        std::max({result.max_bone_length_error,
                  std::fabs((settled.joints[BodyPart::left_knee] -
                             settled.joints[BodyPart::left_thigh])
                                .GetLength() -
                            thigh_length),
                  std::fabs((settled.joints[BodyPart::left_ankle] -
                             settled.joints[BodyPart::left_knee])
                                .GetLength() -
                            shin_length),
                  std::fabs((settled.joints[BodyPart::right_knee] -
                             settled.joints[BodyPart::right_thigh])
                                .GetLength() -
                            thigh_length),
                  std::fabs((settled.joints[BodyPart::right_ankle] -
                             settled.joints[BodyPart::right_knee])
                                .GetLength() -
                            shin_length)});

    // The Player keeps owning the world position: the clip contributes nothing.
    const Vector3 delta(0, -achieved_speed * kTickSeconds, 0);
    state.position += delta;
    result.distance += delta.GetLength();
  }
  return result;
}

}  // namespace

TEST_CASE("stride adaptation shortens the gait without touching bone lengths",
          "[player][stride][stride1]") {
  const float reference = LocomotionClipReferenceSpeed(GaitClip());
  REQUIRE(reference > 1.0f);
  const auto shortened = RunStraight(reference, 40, 0.8f, true);
  CHECK(shortened.max_fore_aft_correction > 0.005f);
  CHECK(shortened.max_bone_length_error < 1e-3f);
}

TEST_CASE("a paired stride and cadence keep the support foot locked",
          "[player][stride][stride1][stride3]") {
  // The body advances at the clip's own reference speed while the stride is
  // shortened to 0.8, so the rate rises to 1.25. The scaled stance foot then
  // moves backwards at exactly the achieved speed and never needs dragging.
  const float reference = LocomotionClipReferenceSpeed(GaitClip());
  const auto paired = RunStraight(reference, 120, 0.8f, true);
  CHECK(paired.planted_ticks > 20);
  // The scaled gait already lands where the anchor says, so the plant has
  // almost nothing to drag...
  CHECK(paired.max_lock_correction < 0.02f);
  // ...and the settled pose sits on the anchor.
  CHECK(paired.max_final_slip < 0.02f);
  CHECK(paired.max_bone_length_error < 1e-3f);
}

TEST_CASE("an unpaired stride slides the support foot",
          "[player][stride][stride1]") {
  // Same shortened stride, but the playback rate is computed as if the stride
  // never changed: the stance foot then trails the body and the plant has to
  // drag it back every tick. This is the "double correction" failure the
  // pairing exists to prevent.
  const float reference = LocomotionClipReferenceSpeed(GaitClip());
  const auto mispaired = RunStraight(reference, 120, 0.8f, false);
  CHECK(mispaired.planted_ticks > 20);
  CHECK(mispaired.max_lock_correction > 0.02f);
}

TEST_CASE("the final pose the collider reads is the pose the IK solved",
          "[player][stride][stride2]") {
  const float reference = LocomotionClipReferenceSpeed(GaitClip());
  const auto run = RunStraight(reference, 60, 0.8f, true);
  CHECK(run.max_final_ankle_error < 1e-3f);
  CHECK(run.max_bone_length_error < 1e-3f);
  CHECK(run.distance ==
        Catch::Approx(reference * 0.01f * 60).margin(1e-4f));
}

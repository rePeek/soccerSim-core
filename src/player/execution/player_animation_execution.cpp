#include "player/execution/player_execution.hpp"
#include "player/player.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <bq_log/bq_log.h>

#include "football/animation/foot_support.hpp"
#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "football/player/execution/player_foot_ik.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_motion.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "player/execution/player_baked_body_pose.hpp"
#include "player/execution/player_diagnostics.hpp"
#include "player/execution/player_motion_constants.hpp"
#include "player/execution/player_runtime_sink.hpp"
#include "player/execution/player_touch_math.hpp"

using namespace football::math;
using football::ball::Ball;

namespace {
constexpr bool animSmoothing = true;
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle =
    animSmoothing ? 0.25f * pi : 0.0f;
// A support foot is handed over only when the other ankle is clearly lower:
// at the double-support instant both feet are at the same height, and a plain
// comparison would flip the stance (and its anchor) every tick.
constexpr float kSupportSwitchMargin = 0.01f;
}  // namespace


PlayerActionState PlayerExecution::CaptureLegacyActionState() const {
  PlayerActionState legacy;
  legacy.type = state_.legacy_anim_state_.functionType;
  legacy.elapsed = {static_cast<std::uint64_t>(state_.legacy_anim_state_.frameNum)};
  legacy.duration = {static_cast<std::uint64_t>(FrameCount())};
  const Anim *anim = &state_.legacy_anim_state_;
  if (anim->touchFrame >= 0)
    legacy.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  legacy.contactPosition = anim->touchPos;
  return legacy;
}

void PlayerExecution::CheckSimulationActionOracle() const {
  const PlayerActionState legacy = CaptureLegacyActionState();
  const bool contactPositionMatches =
      std::memcmp(state_.actionState.contactPosition.coords,
                  legacy.contactPosition.coords,
                  sizeof(state_.actionState.contactPosition.coords)) == 0;
  std::string mismatch;
  if (state_.actionState.type != legacy.type) {
    mismatch = "type";
  } else if (state_.actionState.elapsed != legacy.elapsed) {
    mismatch = "frame";
  } else if (state_.actionState.duration != legacy.duration) {
    mismatch = "frame count";
  } else if (state_.actionState.contact != legacy.contact) {
    mismatch = "contact frame";
  } else if (!contactPositionMatches) {
    mismatch = "contact position bits";
  }
  assert(mismatch.empty() &&
         "authoritative action state diverged from legacy oracle");
}

void PlayerExecution::BeginSimulationAction() {
  const Anim *anim = &state_.legacy_anim_state_;
  PlayerActionDefinition definition;
  definition.type = state_.legacy_anim_state_.functionType;
  definition.duration = {static_cast<std::uint64_t>(FrameCount())};
  if (anim->touchFrame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  definition.contactPosition = anim->touchPos;
  state_.last_begun_definition_ = definition;
  state_.last_begun_start_frame_ = state_.legacy_anim_state_.frameNum;
  PlayerActionExecutor::Begin(state_.actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never derive
  // executor time from the compatibility projection.
  const football::sim::TickSpan initial_elapsed{static_cast<std::uint64_t>(state_.legacy_anim_state_.frameNum)};
  if (initial_elapsed.value > 0) {
    PlayerActionExecutor::Step(state_.actionState, initial_elapsed);
  }
  CheckSimulationActionOracle();
}

// ANIM-1 A6.4 step 4: single Player entry point for touch-animation selection.
  // The evaluation runs against the Player's own touch math; the Player owns
  // the decision and retains the result.
int PlayerExecution::SelectTouchAction(football::sim::Tick now, const football::sim::PlayerTickContext &tick,
                              const football::sim::player::PlayerObservation& observation, const DataSet &sorted_data_set,
                              bool use_desired_movement, const Vector3 &desired_direction,
                              float desired_velocity_float, bool use_desired_body_direction,
                              const Vector3 &desired_body_direction_rel,
                              football::sim::TouchChoice &choice,
                              e_InterruptAnim local_interrupt_anim,
                              bool prefer_pass_and_shot,
                              bool allow_pre_touch_rotation_smuggle,
                              bool use_continuous_ball_check) {
  // ANIM-1 A6.4 step 9c: the loop is Player-owned. The action-execution state
  // supplies the spatial facts and the A8.0 reference-motion kernel.
  // not-yet-migrated spatial facts and the A8.0 reference-motion kernel.
  const int anim_id = SelectBestTouchAnim(
      now, tick, observation, sorted_data_set, use_desired_movement, desired_direction,
      desired_velocity_float, use_desired_body_direction, desired_body_direction_rel,
      choice, local_interrupt_anim, prefer_pass_and_shot,
      allow_pre_touch_rotation_smuggle, use_continuous_ball_check);
  if (anim_id != -1) {
    state_.touch_choice_ = choice;
    ++audit_.touch_selection_successes_;
  }
  return anim_id;
}

bool PlayerExecution::CommitSelectedAction(int selected_anim_id,
                                  const football::sim::TouchChoice &touch_choice,
                                  const PlayerCommand &command,
                                  const football::sim::PlayerTickContext &tick,
                                  football::sim::Tick now,
                                  const football::sim::player::PlayerObservation& observation,
                                  e_InterruptAnim local_interrupt_anim,
                                  int start_frame) {
  // ANIM-1 A6.4 step 7b: the Player owns the install dispatch. The compat
  // projection writes the legacy currentAnim; the Player then installs through
  // the one install entry, and only then records the scheduled-contact audit.
  const bool interrupted =
      state_.actionState.type != e_FunctionType_Movement &&
      !state_.actionState.IsComplete() && !state_.actionState.IsAtLastFrame() &&
      local_interrupt_anim != e_InterruptAnim_None;
  ProjectSelectedActionCompat(selected_anim_id, touch_choice, command, tick, now,
                              observation,
                              start_frame);
  InstallSelectedAction(CurrentSelectedAction());
  state_.action_feedback_.source_action = command.source_action;
  state_.action_feedback_.command_status =
      command.source_action == ControlAction::None
          ? football::sim::player::CommandBuildStatus::MovementOnly
          : football::sim::player::CommandBuildStatus::Accepted;
  state_.action_feedback_.interrupted = interrupted;
  RecordSelectedContactAudit(command);
  return true;
}

void PlayerExecution::InstallSelectedAction(const football::sim::SelectedAction &selection) {
  // ANIM-1 A6.3b.3 (second cut): the Player is now the authority for the action
  // definition - it is built from the Player-owned `SelectedAction`, not read
  // back from the compatibility projection. The compat projection is synced by
  // selector before this call and is cross-checked below. The entry never
  // re-selects a clip or vetoes an interrupt.
  state_.installed_action_ = selection;

  PlayerActionDefinition definition;
  definition.type = selection.function_type;
  definition.duration = {static_cast<std::uint64_t>(selection.frame_count)};
  if (selection.touch_frame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(selection.touch_frame)};
  definition.contactPosition = selection.touch_position;
  state_.last_begun_definition_ = definition;
  state_.last_begun_start_frame_ = selection.start_frame;
  PlayerActionExecutor::Begin(state_.actionState, definition);
  if (selection.start_frame > 0) {
    PlayerActionExecutor::Step(
        state_.actionState, football::sim::TickSpan{static_cast<std::uint64_t>(selection.start_frame)});
  }
  state_.action_feedback_.source_action = ControlAction::None;
  state_.action_feedback_.command_status =
      football::sim::player::CommandBuildStatus::MovementOnly;
  state_.action_feedback_.type = selection.function_type;
  state_.action_feedback_.installed = selection.function_type != e_FunctionType_None;
  state_.action_feedback_.contact_due = false;
  state_.action_feedback_.touch_prepared = false;
  state_.action_feedback_.touch_accepted = false;
  state_.action_feedback_.completed = false;
  state_.action_feedback_.interrupted = false;
  state_.action_feedback_.start_frame = selection.start_frame;
  state_.action_feedback_.touch_frame = selection.touch_frame;
  state_.action_feedback_.prepared_touch_tick = {};
  CheckSimulationActionOracle();

  // Oracle during migration: the compat projection must still agree with
  // the Player-owned selection, otherwise the two authorities have drifted.

  // Same-tick consistency: the committed cursor, the scheduled contact and the
  // compat projection must all agree with the selection immediately, so an
  // install followed by a sample in the same tick cannot be one tick off.
  ++audit_.install_cursor_checks_;
  const bool cursor_same =
      state_.actionState.Frame() == selection.start_frame &&
      state_.actionState.FrameCount() == selection.frame_count &&
      state_.actionState.HasScheduledContact() == (selection.touch_frame >= 0) &&
      state_.actionState.ContactFrame() == selection.touch_frame;
  if (cursor_same) {
    ++audit_.install_cursor_matches_;
  } else {
    ++audit_.install_cursor_mismatches_;
  }
  ++audit_.install_checks_;
  const Anim *anim = &state_.legacy_anim_state_;
  const bool same =
      selection.function_type == state_.legacy_anim_state_.functionType &&
      selection.frame_count == FrameCount() &&
      selection.start_frame == state_.legacy_anim_state_.frameNum &&
      selection.touch_frame == anim->touchFrame &&
      selection.touch_position == anim->touchPos;
  if (same) {
    ++audit_.install_matches_;
  } else {
    ++audit_.install_mismatches_;
  }

}

void PlayerExecution::StepSimulationAction(football::sim::TickSpan elapsed) {
  PlayerActionExecutor::Step(state_.actionState, elapsed);
  CheckSimulationActionOracle();
}

void PlayerExecution::UpdatePlayback(AnimationId clip_id, int frame, float base_angle,
                            int blend_ticks) {
  const AnimationClip& clip = animations_.Get(static_cast<std::uint32_t>(clip_id));
  state_.playback_state_.Play(clip, frame, base_angle,
                         state_.playback_blend_enabled_ ? blend_ticks : 0);
}

void PlayerExecution::UpdateLocomotionPlayback(AnimationId clip_id, float frame_time,
                                     float base_angle, int blend_ticks) {
  const AnimationClip& clip = animations_.Get(static_cast<std::uint32_t>(clip_id));
  state_.playback_state_.PlayAtTime(clip, frame_time, base_angle,
                                  state_.playback_blend_enabled_ ? blend_ticks : 0);
}


PoseFrame PlayerExecution::SettleMovementPose(const PoseFrame& pose,
                                              float base_angle,
                                              const AnimationClip& clip,
                                              float clip_frame_time) {
  // P3b: only ordinary movement carries a support foot. Slide/trip/fall/header
  // intentionally keep both feet free and keep the sampled pose untouched.
  if (state_.installed_action_.function_type != e_FunctionType_Movement) {
    state_.foot_plant_ = PlayerFootPlantState{};
    return pose;
  }
  const PlayerKinematicState& kinematics = port().GetKinematicState();
  const football::player::PlayerBodyProfile body = port().BodyProfile();
  const float final_base_angle = base_angle;
  const PoseFrame sampled_pose = pose;
  const BakedBodyPose gait =
      EvaluateBakedBodyPose(sampled_pose, kinematics, final_base_angle, body);

  // STRIDE-1: the gait's fore-aft excursion is re-aimed at the stride the motion
  // achieved. Support is not decided here.
  PlayerStrideAdaptInput stride;
  stride.left.valid = true;
  stride.left.hip = gait.joints[BodyPart::left_thigh];
  stride.left.ankle = gait.joints[BodyPart::left_ankle];
  stride.right.valid = true;
  stride.right.hip = gait.joints[BodyPart::right_thigh];
  stride.right.ankle = gait.joints[BodyPart::right_ankle];
  stride.facing = Vector3(0, -1, 0).GetRotated2D(final_base_angle);
  stride.stride_scale = state_.stride_scale_;
  stride.gait_phase = state_.locomotion_playback_.gait_phase;
  const PlayerStrideAdaptResult adapted = PlayerPoseAdapter::AdaptStride(stride);
  if (!adapted.targets.has_left && !adapted.targets.has_right) return pose;

  const PlayerFootPlantState previous_plant = state_.foot_plant_;

  // Support detection: the foot that is travelling *backwards* relative to the
  // body is the one carrying weight. Height alone cannot separate a stance foot
  // from one that merely hangs lower, and a settled pose can never decide it at
  // all (a locked foot keeps its height by construction).
  const Vector3 facing = Vector3(0, -1, 0).GetRotated2D(final_base_angle);
  const Vector3 left_offset = gait.joints[BodyPart::left_ankle] -
                              gait.joints[BodyPart::left_thigh];
  const Vector3 right_offset = gait.joints[BodyPart::right_ankle] -
                               gait.joints[BodyPart::right_thigh];
  const float left_fore_aft = left_offset.GetDotProduct(facing);
  const float right_fore_aft = right_offset.GetDotProduct(facing);
  PlayerFootSupportInput support_input;
  // Positive = the ankle moved backwards along the facing axis since last tick.
  support_input.left_backward_rate =
      state_.previous_fore_aft_[0] - left_fore_aft;
  support_input.right_backward_rate =
      state_.previous_fore_aft_[1] - right_fore_aft;
  support_input.have_previous = state_.have_previous_fore_aft_;
  support_input.previous_support_left = state_.support_left_;
  support_input.have_previous_support = state_.have_support_;
  support_input.switch_margin = kSupportSwitchMargin;
  // The clip's own support track answers this question; the velocity rule is the
  // fallback for clips without one (and keeps the history warm for them).
  const FootSupportFrame track = SampleFootSupport(clip, clip_frame_time);
  PlayerFootSupportResult support;
  if (FootSupportTrusted(clip.foot_support_summary, !clip.foot_support.empty())) {
    support.left_support = FootIsSupporting(track.left);
    support.right_support = FootIsSupporting(track.right);
    // The baked phases assume the clip is played along its own direction. When
    // the Player turns while the animation is still "stance", the foot is
    // pivoting on the ground and must not be locked to a fixed world point: the
    // world motion has the final say on whether the foot is standing still.
    if (state_.have_previous_fore_aft_) {
      const auto world_speed = [&](BodyPart ankle, int index) {
        return (gait.joints[ankle] - state_.previous_world_ankle_[index])
                   .Get2D()
                   .GetLength() /
               0.01f;
      };
      const float left_world = world_speed(BodyPart::left_ankle, 0);
      const float right_world = world_speed(BodyPart::right_ankle, 1);
      if (support.left_support && left_world > kStanceMaxWorldSpeed) {
        support.left_support = false;
      }
      if (support.right_support && right_world > kStanceMaxWorldSpeed) {
        support.right_support = false;
      }
    }
  } else {
    support = SelectFootSupport(support_input);
  }
  // RMA-0: measure the slip the settled support actually shipped, on the same
  // adapted sample the root was committed with. Only a foot supported on both
  // sides of the interval counts.
  if (state_.adapted_locomotion_valid_) {
    state_.adapted_locomotion_.diagnostics.left_support_slip =
        MeasureSupportSlip(previous_plant.left_planted, support.left_support,
                           state_.previous_world_ankle_[0],
                           gait.joints[BodyPart::left_ankle]);
    state_.adapted_locomotion_.diagnostics.right_support_slip =
        MeasureSupportSlip(previous_plant.right_planted, support.right_support,
                           state_.previous_world_ankle_[1],
                           gait.joints[BodyPart::right_ankle]);
  }
  state_.previous_world_ankle_[0] = gait.joints[BodyPart::left_ankle];
  state_.previous_world_ankle_[1] = gait.joints[BodyPart::right_ankle];
  state_.previous_fore_aft_[0] = left_fore_aft;
  state_.previous_fore_aft_[1] = right_fore_aft;
  state_.have_previous_fore_aft_ = true;
  state_.support_left_ = support.left_support;
  state_.have_support_ = support.left_support != support.right_support;

  PlayerFootPlantInput plant_input;
  // The anchor of a newly planted foot is the stride-adapted ankle, i.e. exactly
  // where the settled foot would be: planting never moves the foot.
  plant_input.left_ankle = adapted.targets.left;
  plant_input.right_ankle = adapted.targets.right;
  plant_input.left_support = support.left_support;
  plant_input.right_support = support.right_support;
  plant_input.plant_height = 0.22f * body.height_scale;
  state_.foot_plant_ = PlayerFootIk::Update(previous_plant, plant_input);

  // STRIDE-0: how far the stride-adapted gait sits from the anchor a foot is
  // still locked on. Only a foot that stays planted across the tick counts: at
  // the handover tick the foot is being released, not dragged. With a paired
  // cadence/stride this is discretisation only; when the rate and the stride
  // disagree it is the drift the plant has to absorb.
  const PlayerFootPlantSlip drift = MeasureFootPlantSlip(
      previous_plant, adapted.targets.left, adapted.targets.right);
  if (previous_plant.left_planted && state_.foot_plant_.left_planted) {
    state_.locomotion_diagnostics_.left_animation_slip = drift.left;
  }
  if (previous_plant.right_planted && state_.foot_plant_.right_planted) {
    state_.locomotion_diagnostics_.right_animation_slip = drift.right;
  }

  // STRIDE-2: the same solve has to live in the skeleton, not only in a
  // world-space joint list, so Pose(), the baked sample, the colliders and the
  // touch geometry cannot disagree about where the feet are. The support
  // authority overrides the gait for planted feet.
  const PlayerFootIkTargets targets =
      ApplyFootPlantLocks(state_.foot_plant_, adapted.targets);
  const BakedPoseFootSolve first = ApplyWorldFootTargetsToPose(
      sampled_pose, kinematics, final_base_angle, body, targets);

  // P1: an anchor the leg can no longer reach must not pin the leg at maximum
  // extension. It slides toward the foot by a bounded step (the foot really does
  // move when the body was pushed away), and only a discontinuity releases it.
  // The relaxation target is where the animation's foot actually is, not the
  // locked anchor it was replaced with.
  const PlayerFootPlantRelax relax = RelaxFootPlant(
      state_.foot_plant_, adapted.targets, first.left_residual,
      first.right_residual);
  const bool plant_changed =
      relax.left_slide > 0.0f || relax.right_slide > 0.0f ||
      relax.left_released || relax.right_released;
  if (relax.left_released || relax.right_released) {
    state_.locomotion_diagnostics_.left_plant_released = relax.left_released;
    state_.locomotion_diagnostics_.right_plant_released = relax.right_released;
    ++audit_.plant_releases_;
  }
  state_.locomotion_diagnostics_.left_plant_slide = relax.left_slide;
  state_.locomotion_diagnostics_.right_plant_slide = relax.right_slide;
  state_.foot_plant_ = relax.plant;

  // The pose must match the plant state it ships with. If the anchor moved or was
  // released, solve once more against the new anchors: bounded (only on the ticks
  // that relaxed) and it is what keeps "Final Pose == contact state".
  BakedPoseFootSolve solved = first;
  if (plant_changed) {
    const PlayerFootIkTargets relaxed_targets =
        ApplyFootPlantLocks(state_.foot_plant_, adapted.targets);
    solved = ApplyWorldFootTargetsToPose(sampled_pose, kinematics,
                                         final_base_angle, body, relaxed_targets);
    ++audit_.pose_resolves_;
  }

  // A planted leg targets its anchor, so the solve residual *is* the slide the
  // final pose ships. An unplanted leg reports nothing.
  state_.locomotion_diagnostics_.left_foot_slip =
      state_.foot_plant_.left_planted ? solved.left_residual : 0.0f;
  state_.locomotion_diagnostics_.right_foot_slip =
      state_.foot_plant_.right_planted ? solved.right_residual : 0.0f;
  // Where a residual comes from matters: a locked support anchor the leg cannot
  // reach is a plant problem, a swinging foot's own target out of reach is a clip
  // or stride problem.
  state_.locomotion_diagnostics_.max_plant_residual = std::max(
      state_.foot_plant_.left_planted ? solved.left_residual : 0.0f,
      state_.foot_plant_.right_planted ? solved.right_residual : 0.0f);
  state_.locomotion_diagnostics_.max_swing_residual = std::max(
      !state_.foot_plant_.left_planted ? solved.left_residual : 0.0f,
      !state_.foot_plant_.right_planted ? solved.right_residual : 0.0f);
  return solved.pose;
}

void PlayerExecution::ComposeFinalPose(const AnimationClip& clip,
                                       bool motion_driven) {
  if (!state_.playback_state_.have_pose) {
    state_.final_pose_valid_ = false;
    return;
  }
  const football::player::PlayerBodyProfile body = port().BodyProfile();
  const PoseFrame blended = state_.playback_state_.final_pose;
  const float blended_base_angle = state_.playback_state_.final_base_angle;

  // 1. P3a warp: the body yaw and pelvis bob follow the motion the Player
  //    actually achieved. Locomotion only - one-shot actions keep their baked
  //    root bearing, which the action pipeline already owns.
  PoseFrame warped = blended;
  float warped_base_angle = blended_base_angle;
  if (motion_driven && state_.locomotion_animation_input_.has_value()) {
    const LocomotionAnimationInput& locomotion = *state_.locomotion_animation_input_;
    PlayerPoseWarpInput warp;
    warp.reference_pose = blended;
    warp.reference_base_angle = blended_base_angle;
    // The clip track is local; the comparison is in world space.
    warp.reference_delta =
        state_.locomotion_reference_delta_.Get2D().GetRotated2D(blended_base_angle);
    warp.achieved_delta =
        (locomotion.next.position - locomotion.current.position).Get2D();
    warp.body = body;
    // The player's own vertical style scales the pelvis bob; the warp's own
    // bob ratio still follows the achieved distance.
    warp.pelvis_bob_scale = animation_config_.Get(clip.id).pelvis_bob_scale;
    // A pose correction, not a turn: bound how far the body yaw may be
    // corrected in one tick.
    warp.max_yaw_correction = kPlayerPoseWarpMaxYawPerTick;
    const PlayerPoseWarpResult warped_result = PlayerPoseAdapter::Warp(warp);
    warped = warped_result.pose;
    warped_base_angle = warped_result.base_angle;
  }

  // 2. M3 bounded realization: no joint may turn faster than a body can. This
  //    runs before the foot solve on purpose - the IK and the plant lock are
  //    the last word on the feet, so a rate limit can never undo a locked foot.
  PlayerPoseRealizeInput realize;
  realize.previous_pose =
      state_.final_pose_valid_ ? state_.final_pose_ : warped;
  realize.target_pose = warped;
  realize.body = body;
  // Clip entry damps the follow budget; the action cursor is the clip-time
  // authority for both action clips and the re-installed movement clips.
  realize.entry_weight = std::sqrt(football::math::curve(
      1.0f - football::math::NormalizedClamp(
                 static_cast<float>(state_.actionState.Frame()), 0.0f, 8.0f),
      1.0f));
  realize.outgoing_special_state =
      !clip.metadata.outgoing_special_state.empty();
  const PlayerPoseRealizeResult realized = PlayerPoseAdapter::Realize(realize);

  // 3. STRIDE: settle the movement pose against the achieved motion - stride
  //    target, support, plant lock and the local-rotation write-back. Support,
  //    anchor and shipped pose are decided together, so no anchor is ever
  //    captured on a pose nobody sees and no foot stays locked after the gait
  //    already lifted it.
  state_.settle_source_pose_ = realized.pose;
  state_.settle_source_valid_ = true;
  // The support track is a clip-time fact: locomotion ticks sample the
  // motion-driven clip time, action-clock ticks the action cursor.
  const float clip_frame_time =
      motion_driven ? state_.locomotion_playback_.frame_time
                    : static_cast<float>(state_.actionState.Frame());
  state_.settle_clip_frame_time_ = clip_frame_time;
  const PoseFrame settled = SettleMovementPose(realized.pose, warped_base_angle,
                                               clip, clip_frame_time);

  // P1: the realization bounds the *incoming animation*, and the plant solve has
  // the last word on the legs. Report how fast the shipped leg joints actually
  // turn, so a support correction that exceeds the animation budget is visible
  // instead of silently reintroducing foot slide through a second limiter.
  if (state_.final_pose_valid_) {
    float peak = 0.0f;
    for (const BodyPart joint : {BodyPart::left_thigh, BodyPart::right_thigh,
                                 BodyPart::left_knee, BodyPart::right_knee,
                                 BodyPart::left_ankle, BodyPart::right_ankle}) {
      auto before = state_.final_pose_.orientations[joint];
      auto after = settled.orientations[joint];
      const float dot = before.MakeSameNeighborhood(after);
      peak = std::max(peak, 2.0f * std::acos(std::clamp(dot, -1.0f, 1.0f)));
    }
    state_.locomotion_diagnostics_.max_leg_joint_step = peak;
    if (state_.adapted_locomotion_valid_) {
      state_.adapted_locomotion_.diagnostics.max_joint_rotation_step = peak;
    }
  }

  // 4. The single Final Pose. Pose(), the baked sample, the colliders, the
  //    touch geometry and the renderer all read this one value.
  state_.final_pose_ = settled;
  state_.final_pose_base_angle_ = warped_base_angle;
  state_.final_pose_valid_ = true;
  // A later clip switch must blend from what the body actually looked like.
  state_.playback_state_.SetBlendSource(state_.final_pose_,
                                       state_.final_pose_base_angle_);
}

bool PlayerExecution::ResettleFinalPose(PlayerExecutionPort& owner) {
  PortScope scope(*this, owner);
  if (!state_.final_pose_valid_ || !state_.settle_source_valid_) return false;
  if (state_.installed_action_.function_type != e_FunctionType_Movement) {
    return false;
  }
  // M4: a root that changed after the tick invalidates the settled pose, not the
  // clip. Re-run the same settle on the same realized pose with the corrected
  // root; the plant anchors are world-fixed, so the legs re-reach the feet and
  // the residual stays bounded by the IK reach. No clip time is consumed.
  const PoseFrame settled =
      SettleMovementPose(state_.settle_source_pose_, state_.final_pose_base_angle_,
                         GetCurrentBakedClip(), state_.settle_clip_frame_time_);
  state_.final_pose_ = settled;
  state_.final_pose_valid_ = true;
  state_.playback_state_.SetBlendSource(state_.final_pose_,
                                       state_.final_pose_base_angle_);
  ++audit_.pose_resolves_;
  return true;
}

AdaptedMotionSample PlayerExecution::EvaluateAdaptedLocomotion(
    const PlayerKinematicState& current,
    const football::math::Vector3& desired_velocity,
    const football::math::Vector3& idle_facing,
    const PlayerMotionProfile& capability,
    const football::player::PlayerBodyProfile& body, float dt) {
  // The authority clip is already installed by the action tick; ordinary
  // locomotion never selects one here.
  const AnimationId clip_id = state_.installed_action_.clip_id >= 0
                                 ? state_.installed_action_.clip_id
                                 : state_.legacy_anim_state_.animationId;
  const AnimationClip& clip = GetBakedClip(clip_id);

  const ClipTuning& tuning = animation_config_.Get(clip_id);
  PlayerActionControl control;
  control.desired_velocity = desired_velocity;
  control.idle_facing = idle_facing;
  control.authority = PlayerMotionAuthority::Locomotion;
  control.base_angle =
      state_.startAngle + state_.legacy_anim_state_.rotationSmuggleOffset;
  control.gait.stride_preference = tuning.stride_preference;
  control.gait.cadence_preference = tuning.cadence_preference;
  control.gait.stride_response = tuning.stride_response;
  control.gait.min_stride_scale = kLocomotionMinStrideScale;
  // Reach-aware ceiling first: the adapter derives the playback rate from the
  // step it is actually allowed to take.
  control.gait.max_stride_scale =
      std::min(tuning.max_stride_scale, kLocomotionMaxStrideScale);

  PlayerPlaybackState playback;
  const bool same_clip =
      state_.locomotion_playback_.clip_id == clip_id;
  playback.frame_time =
      same_clip ? state_.locomotion_playback_.frame_time
                : static_cast<float>(state_.last_begun_start_frame_);
  playback.stride_scale = state_.stride_scale_;
  playback.entry_frame_time =
      static_cast<float>(state_.last_begun_start_frame_);

  const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
      clip, current, capability, body, control, playback, dt);
  state_.adapted_locomotion_ = sample;
  state_.adapted_locomotion_valid_ = sample.feasible;
  return sample;
}


void PlayerExecution::UpdatePlaybackFromAuthority(football::sim::Tick now,
                                                 float base_angle, int blend_ticks,
                                                 bool motion_driven) {
  const AnimationId authority_clip = state_.installed_action_.clip_id;
  const int authority_frame = state_.actionState.Frame();
  // ANIM-2 A10.1: at most one real playback update per tick. A repeated call
  // in the same tick is a pure read and must not advance the transition twice.
  if (state_.last_playback_tick_ && *state_.last_playback_tick_ == now) return;
  state_.last_playback_tick_ = now;
  if (motion_driven) {
    // M2/RMA-3: ordinary locomotion is interpolated on the motion the Player
    // actually achieved. The clip is still authority-owned; its time, rate and
    // gait phase come from the same adapted sample that produced the root.
    const AnimationClip& clip = GetBakedClip(authority_clip);
    const bool use_adapted =
        state_.adapted_locomotion_valid_ &&
        state_.adapted_locomotion_.clip_id == authority_clip;
    if (use_adapted) {
      const AdaptedMotionSample& adapted = state_.adapted_locomotion_;
      state_.stride_scale_ = adapted.stride_scale;
      state_.locomotion_playback_ = LocomotionPlaybackRequest{};
      state_.locomotion_playback_.clip_id = authority_clip;
      state_.locomotion_playback_.frame_time = adapted.clip_time;
      state_.locomotion_playback_.playback_rate = adapted.playback_rate;
      state_.locomotion_playback_.gait_phase = adapted.gait_phase;
      state_.locomotion_playback_.stride_scale = adapted.stride_scale;
      state_.locomotion_playback_.looped = adapted.looped;
      UpdateLocomotionPlayback(authority_clip, adapted.clip_time, base_angle,
                               blend_ticks);
      state_.locomotion_reference_delta_ =
          adapted.reference_delta_local.Get2D();
      state_.locomotion_diagnostics_.achieved_distance =
          adapted.diagnostics.adapted_delta.Get2D().GetLength();
      state_.locomotion_diagnostics_.reference_distance =
          state_.locomotion_reference_delta_.GetLength();
      state_.locomotion_diagnostics_.playback_rate = adapted.playback_rate;
      state_.locomotion_diagnostics_.stride_scale = adapted.stride_scale;
      state_.locomotion_diagnostics_.reach_limited =
          adapted.diagnostics.reach_limited;
    } else {
      // Compatibility fallback: the adapter did not produce a sample for this
      // tick (reset or a non-motion path). Keep the established derivation so
      // no tick loses its clip time.
      const LocomotionAnimationInput& locomotion =
          *state_.locomotion_animation_input_;
      const float achieved_speed = locomotion.next.velocity.GetLength();
      const float entry_frame_time =
          static_cast<float>(state_.last_begun_start_frame_);
      const float reference_speed = LocomotionEffectiveGaitSpeed(
          clip, port().BodyProfile().height_scale);
      const ClipTuning& tuning = animation_config_.Get(authority_clip);
      LocomotionStrideStyle style;
      style.stride_preference = tuning.stride_preference;
      style.cadence_preference = tuning.cadence_preference;
      style.stride_response = tuning.stride_response;
      const float reach_ceiling =
          std::min(tuning.max_stride_scale, kLocomotionMaxStrideScale);
      const float target_stride = std::clamp(
          LocomotionStrideScaleFor(style, achieved_speed, reference_speed),
          kLocomotionMinStrideScale, reach_ceiling);
      state_.stride_scale_ =
          SmoothLocomotionStrideScale(state_.stride_scale_, target_stride, 0.01f);
      state_.stride_scale_ = std::min(state_.stride_scale_, reach_ceiling);
      state_.locomotion_diagnostics_.reach_limited =
          target_stride <
                  LocomotionStrideScaleFor(style, achieved_speed, reference_speed) -
              1e-6f;
      const LocomotionPlaybackRequest previous = state_.locomotion_playback_;
      state_.locomotion_playback_ = AdvanceLocomotionPlayback(
          previous, authority_clip, clip, achieved_speed, reference_speed,
          entry_frame_time, 0.01f, state_.stride_scale_);
      UpdateLocomotionPlayback(authority_clip,
                               state_.locomotion_playback_.frame_time, base_angle,
                               blend_ticks);
      const float start_time = previous.clip_id == authority_clip
                                   ? previous.frame_time
                                   : entry_frame_time;
      state_.locomotion_diagnostics_.achieved_distance =
          (locomotion.next.position - locomotion.current.position)
              .Get2D()
              .GetLength();
      const football::math::Vector3 reference_delta =
          LocomotionReferenceDelta(clip.root_positions, start_time,
                                   state_.locomotion_playback_.frame_time)
              .Get2D();
      state_.locomotion_reference_delta_ = reference_delta;
      state_.locomotion_diagnostics_.reference_distance =
          reference_delta.GetLength();
      state_.locomotion_diagnostics_.playback_rate =
          state_.locomotion_playback_.playback_rate;
      state_.locomotion_diagnostics_.stride_scale =
          state_.locomotion_playback_.stride_scale;
    }
  } else {
    state_.locomotion_playback_ = LocomotionPlaybackRequest{};
    state_.locomotion_reference_delta_ = football::math::Vector3(0);
    state_.locomotion_diagnostics_ = LocomotionPoseDiagnostics{};
    state_.stride_scale_ = 1.0f;
    state_.adapted_locomotion_ = AdaptedMotionSample{};
    state_.adapted_locomotion_valid_ = false;
    UpdatePlayback(authority_clip, authority_frame, base_angle, blend_ticks);
  }
  ComposeFinalPose(GetBakedClip(authority_clip), motion_driven);
  // ANIM-1 A6.3d: the compat projection no longer decides the clip or the
  // action time. Verify the executor-owned artefacts agree - the installed
  // selection and the pose that was sampled. A motion-driven tick owns clip
  // time, so only the clip identity is checked there; the action cursor must
  // still agree for every action tick.
  ++audit_.authority_checks_;
  const bool same = state_.playback_state_.clip_id == authority_clip &&
                    (motion_driven || state_.playback_state_.frame == authority_frame);
  if (same) {
    ++audit_.authority_matches_;
  } else {
    ++audit_.authority_mismatches_;
  }
}

void PlayerExecution::AdvanceActionTick() {
  // ANIM-1 A6.3c.2: one real time advance. The PlayerActionState cursor is the
  // authority; the compatibility integer frame is a read-only projection.
  PlayerActionExecutor::Step(state_.actionState, football::sim::TickSpan{1});
  state_.legacy_anim_state_.frameNum = state_.actionState.Frame();
  state_.action_feedback_.type = state_.actionState.type;
  state_.action_feedback_.contact_due =
      state_.actionState.HasScheduledContact() && state_.actionState.IsContactDue();
  state_.action_feedback_.completed = state_.actionState.IsComplete();
  CheckSimulationActionOracle();
}

const AnimationClip &PlayerExecution::GetBakedClip(AnimationId id) const {
  return animations_.Get(static_cast<uint32_t>(id));
}

football::sim::SelectedAction PlayerExecution::CurrentSelectedAction() const {
  football::sim::SelectedAction selection;
  selection.clip_id = state_.legacy_anim_state_.animationId;
  selection.function_type = state_.legacy_anim_state_.functionType;
  selection.start_frame = state_.legacy_anim_state_.frameNum;
  selection.frame_count = static_cast<int>(GetBakedClip(state_.legacy_anim_state_.animationId).frame_count);
  selection.touch_frame = state_.legacy_anim_state_.touchFrame;
  selection.touch_position = state_.legacy_anim_state_.touchPos;
  return selection;
}

const AnimationClip &PlayerExecution::GetCurrentBakedClip() const {
  return GetBakedClip(state_.legacy_anim_state_.animationId);
}

// ---------------------------------------------------------------------------
// ANIM-1 A9.5c/A9.8: the compat install projection and the pose assembly,
// relocated verbatim from Humanoid::ProjectSelectedActionCompat /
// Humanoid::RecordSelectedContactAudit and HumanoidBase::SampleBakedPose.
// ---------------------------------------------------------------------------
PlayerBakedPoseSample PlayerExecution::SampleBakedPose(
    const PlayerKinematicState& state,
    const football::player::PlayerBodyProfile& profile) const {
  const auto& clip = GetCurrentBakedClip();
  if (clip.poses.empty() || state_.legacy_anim_state_.frameNum < 0)
    throw std::logic_error("missing runtime baked pose frame");
  const auto frame = std::min(std::size_t(state_.legacy_anim_state_.frameNum), clip.poses.size() - 1);
  PlayerBakedPoseSample sample;
  sample.pose = clip.poses[frame];
  sample.state = state;
  sample.animation_id = state_.legacy_anim_state_.animationId;
  sample.requested_frame = static_cast<std::int64_t>(state_.legacy_anim_state_.frameNum);
  sample.sampled_frame = static_cast<std::int64_t>(frame);
  sample.frame_clamped = sample.requested_frame != sample.sampled_frame;
  sample.base_angle = state_.startAngle + state_.legacy_anim_state_.rotationSmuggleOffset;
  sample.body_profile = profile;
  sample.action = state_.legacy_anim_state_.functionType;
  sample.outgoing_special_state = !clip.metadata.outgoing_special_state.empty();
  return sample;
}

// ANIM-2 A11.0/A11.5/A11.7: a read-only raw-clip pose preview. It never advances
// the action clock, draws RNG or mutates the Player. For a positive offset it
// returns the raw clip skeleton at the requested future frame (not the blended
// pose), which is exactly the body-forecast input the Simulation used to build
// by indexing the library itself. The offset is added in 64 bits and clamped to
// the clip range in BOTH directions, so neither a negative offset nor an extreme
// positive one can overflow or index out of bounds.
PlayerBakedPoseSample PlayerExecution::PreviewRawPose(PlayerExecutionPort& owner,
                                                 int frame_offset) const {
  PlayerBakedPoseSample sample = BakedPoseSample(owner);
  const auto& clip =
      animations_.Get(static_cast<std::uint32_t>(sample.animation_id));
  const std::int64_t requested =
      sample.requested_frame + static_cast<std::int64_t>(frame_offset);
  const std::int64_t last_frame =
      static_cast<std::int64_t>(clip.poses.size()) - 1;
  const std::int64_t sampled =
      std::clamp(requested, std::int64_t{0}, last_frame);
  sample.requested_frame = requested;
  sample.sampled_frame = sampled;
  sample.frame_clamped = sampled != requested;
  sample.pose = clip.poses[static_cast<std::size_t>(sampled)];
  sample.outgoing_special_state = !clip.metadata.outgoing_special_state.empty();
  // A raw-clip preview is not a settled pose: it must not inherit this tick's
  // support anchors, or the "original reference pose" would silently carry the
  // current plant constraints.
  sample.foot_targets = PlayerFootIkTargets{};
  return sample;
}

void PlayerExecution::ProjectSelectedActionCompat(
    int selectedAnimID, const football::sim::TouchChoice &touch_choice,
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, const football::sim::player::PlayerObservation& observation,
    int start_frame) {
  state_.previousAnim_functionType = state_.legacy_anim_state_.functionType;

  state_.legacy_anim_state_.animationId = selectedAnimID;
  state_.legacy_anim_state_.functionType = command.desiredFunctionType;
  // ANIM-2 A13: install at the matched entry frame. 0 is the legacy cursor.
  state_.legacy_anim_state_.frameNum = start_frame;
  state_.legacy_anim_state_.touchFrame = touch_choice.touch_frame;
  state_.legacy_anim_state_.touchPos = touch_choice.touch_position;
  state_.legacy_anim_state_.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, state_.legacy_spatial_state_.relBodyAngleNonquantized - GetCurrentBakedClip().metadata.incoming_body_angle) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (state_.legacy_anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (state_.legacy_anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
  state_.legacy_anim_state_.rotationSmuggle.end = touch_choice.rotation_smuggle;
  state_.legacy_anim_state_.rotationSmuggleOffset = 0;
  state_.legacy_anim_state_.actionSmuggle = touch_choice.action_smuggle;
  state_.legacy_anim_state_.actionSmuggleOffset = Vector3(0);
  state_.legacy_anim_state_.actionSmuggleSustain = Vector3(0); // calculated below
  state_.legacy_anim_state_.actionSmuggleSustainOffset = Vector3(0);
  state_.legacy_anim_state_.movementSmuggle = Vector3(0); // needs to be reset here, else the previous calc is used in upcoming 'calculatemovementsmuggle'
  state_.legacy_anim_state_.movementSmuggleOffset = Vector3(0);
  state_.legacy_anim_state_.incomingMovement = state_.legacy_spatial_state_.movement;
  state_.legacy_anim_state_.outgoingMovement = CalculateOutgoingMovement(touch_choice.positions);
  state_.legacy_anim_state_.positions.clear();
  state_.legacy_anim_state_.positions.assign(touch_choice.positions.begin(), touch_choice.positions.end());
  // ANIM-2 A13: a non-zero entry frame skips the clip's earlier root motion, so
  // re-base the reference trajectory to the entry frame. Otherwise the first
  // committed tick would add the whole skipped displacement as a teleport. A
  // constant shift leaves every delta (outgoing movement, smuggles) unchanged.
  if (start_frame > 0 && !state_.legacy_anim_state_.positions.empty()) {
    const std::size_t index =
        std::min(static_cast<std::size_t>(start_frame),
                 state_.legacy_anim_state_.positions.size() - 1);
    const Vector3 base = state_.legacy_anim_state_.positions[index];
    for (Vector3 &p : state_.legacy_anim_state_.positions) p -= base;
  }
  state_.legacy_anim_state_.positionOffset = Vector3(0);
  state_.legacy_anim_state_.originatingCommand = command;
  // Must run after the state_.legacy_anim_state_ field writes above (it reads the new
  // animationId/positions/touchFrame and GetCurrentBakedClip), before install.
  state_.legacy_anim_state_.movementSmuggle = ComputeMovementSmuggle(now, tick, observation);
  state_.legacy_anim_state_.movementSmuggleOffset = Vector3(0);
}

void PlayerExecution::RecordSelectedContactAudit(const PlayerCommand &command) {
  const PlayerActionState &scheduled = ActionState();
  if (ContactAuthorityAuditEnabled() &&
      IsTrackedScheduledContact(scheduled.type) &&
      scheduled.HasScheduledContact()) {
    ContactAuthorityAudit &audit = ContactAuthorityFor(scheduled.type);
    ++audit.scheduled;
    audit.contact_frames.push_back(scheduled.ContactFrame());
    audit.delays_ms.push_back(static_cast<int>(football::sim::ToMilliseconds(*scheduled.contact)));
    if (command.modifier & e_PlayerCommandModifier_KnockOn) ++audit.knock_on;
    if (command.touchInfo.targetPlayer) ++audit.command_target;
    if (command.touchInfo.forcedTargetPlayer) ++audit.forced_target;
    if (GetCurrentBakedClip().metadata.touch_max_power_factor != 0.0f)
      ++audit.max_power_profile_present;
    if (GetCurrentBakedClip().metadata.touch_difficulty_factor != 0.0f)
      ++audit.difficulty_profile_present;
    if (GetCurrentBakedClip().metadata.outgoing_ball_direction.GetLength() != 0.0f)
      ++audit.native_ball_direction_present;
    if (!GetCurrentBakedClip().metadata.touch_bodypart.empty())
      ++audit.contact_bodypart_present;
    if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty())
      ++audit.incoming_retain_present;
    if (!GetCurrentBakedClip().metadata.outgoing_retain_state.empty())
      ++audit.outgoing_retain_present;
  }
}

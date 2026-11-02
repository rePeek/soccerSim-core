#include "player/execution/player_execution.hpp"
#include "player/player.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <bq_log/bq_log.h>

#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_pose_adapter.hpp"
#include "player/execution/player_diagnostics.hpp"
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
                              const football::sim::player::PlayerObservation& observation, const std::vector<int> &sorted_data_set,
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



void PlayerExecution::ComposeFinalPose(const AnimationClip& clip,
                                       bool motion_driven) {
  if (!state_.playback_state_.have_pose) {
    state_.final_pose_valid_ = false;
    return;
  }
  const football::player::PlayerBodyProfile body = port().BodyProfile();
  // RMA-2/RMA-7: in steady locomotion the pose the adapter produced *is* the
  // source pose. Playback still owns the cross-clip blend, so a switch keeps
  // its transition; otherwise the adapter's sample is not re-sampled here.
  const bool use_adapted_pose =
      motion_driven && state_.adapted_locomotion_valid_ &&
      !state_.playback_state_.transition.active;
  const PoseFrame& blended = use_adapted_pose
                                 ? state_.adapted_locomotion_.pose
                                 : state_.playback_state_.final_pose;
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

  // 2. M3 bounded realization: no joint may turn faster than a body can.
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

  // 3. RMA-7: the realized pose is the Final Pose. There is no runtime support
  //    plant or two-bone foot solve; support correctness comes from the
  //    clip's calibrated cadence and the speed cap applied to the root.
  const PoseFrame settled = realized.pose;

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


void PlayerExecution::NoteRootCorrection(const football::math::Vector3& offset) {
  // RMA-6/RM-7: a correction is the passive displacement stage. Record the
  // offset so final_delta can be decomposed into active root + passive
  // correction; the pose itself is re-derived below from the corrected root.
  if (state_.adapted_locomotion_valid_) {
    state_.adapted_locomotion_.diagnostics.collision_limited = true;
    state_.adapted_locomotion_.diagnostics.passive_correction +=
        offset.Get2D();
  }
  if (state_.adapted_action_valid_) {
    state_.adapted_action_.diagnostics.collision_limited = true;
    state_.adapted_action_.diagnostics.passive_correction += offset.Get2D();
  }
}

bool PlayerExecution::ResettleFinalPose(PlayerExecutionPort& owner) {
  PortScope scope(*this, owner);
  if (!state_.final_pose_valid_) return false;
  if (state_.installed_action_.function_type != e_FunctionType_Movement) {
    return false;
  }
  // RMA-7: a corrected root re-derives the *warp* from the same adapted
  // reference pose, with the corrected displacement. No clip time is consumed
  // and no foot solve exists to re-run - the legs never depended on the root.
  if (!state_.adapted_locomotion_valid_) return false;
  const AnimationClip& clip = GetCurrentBakedClip();
  const football::player::PlayerBodyProfile body = port().BodyProfile();
  const float base_angle =
      state_.startAngle + state_.legacy_anim_state_.rotationSmuggleOffset;
  PlayerPoseWarpInput warp;
  warp.reference_pose = state_.adapted_locomotion_.pose;
  warp.reference_base_angle = base_angle;
  warp.reference_delta =
      state_.locomotion_reference_delta_.Get2D().GetRotated2D(base_angle);
  warp.achieved_delta =
      (port().GetKinematicState().position -
       state_.adapted_locomotion_start_position_)
          .Get2D();
  warp.body = body;
  warp.pelvis_bob_scale = animation_config_.Get(clip.id).pelvis_bob_scale;
  warp.max_yaw_correction = kPlayerPoseWarpMaxYawPerTick;
  const PlayerPoseWarpResult warped = PlayerPoseAdapter::Warp(warp);

  PlayerPoseRealizeInput realize;
  realize.previous_pose = state_.final_pose_;
  realize.target_pose = warped.pose;
  realize.body = body;
  realize.entry_weight = 0.0f;
  realize.outgoing_special_state =
      !clip.metadata.outgoing_special_state.empty();
  const PlayerPoseRealizeResult realized = PlayerPoseAdapter::Realize(realize);
  state_.final_pose_ = realized.pose;
  state_.final_pose_base_angle_ = warped.base_angle;
  state_.final_pose_valid_ = true;
  // A collision correction is accepted as passive displacement. With no runtime
  // foot solve, a support foot may slide under an external push; that is the
  // explicit model, not a hidden IK correction.
  state_.adapted_locomotion_.diagnostics.final_delta =
      port().GetKinematicState().position -
      state_.adapted_locomotion_start_position_;
  // RM-7: the decomposition is a contract, not only a report. The external
  // correction is the only difference between the active sample and the
  // committed root.
  const football::math::Vector3 decomposition_residual =
      state_.adapted_locomotion_.diagnostics.final_delta -
      state_.adapted_locomotion_.diagnostics.adapted_delta -
      state_.adapted_locomotion_.diagnostics.passive_correction;
  assert(decomposition_residual.Get2D().GetLength() < 1e-3f &&
         "active root + passive correction must equal the final displacement");
  static_cast<void>(decomposition_residual);
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

  PlayerActionControl control;
  control.desired_velocity = desired_velocity;
  control.idle_facing = idle_facing;
  control.authority = PlayerMotionAuthority::Locomotion;
  control.base_angle =
      state_.startAngle + state_.legacy_anim_state_.rotationSmuggleOffset;

  PlayerPlaybackState playback;
  const bool same_clip =
      state_.locomotion_playback_.clip_id == clip_id;
  playback.frame_time =
      same_clip ? state_.locomotion_continuous_time_
                : static_cast<float>(state_.last_begun_start_frame_);
  playback.stride_scale = state_.stride_scale_;
  playback.entry_frame_time =
      static_cast<float>(state_.last_begun_start_frame_);

  const AdaptedMotionSample sample = PlayerActionAdapter::Evaluate(
      clip, current, capability, body, control, playback, dt);
  state_.adapted_locomotion_ = sample;
  state_.adapted_locomotion_valid_ = sample.feasible;
  state_.adapted_locomotion_start_position_ = current.position;
  state_.locomotion_diagnostics_.turn_limited =
      sample.diagnostics.turn_limited;
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
      state_.locomotion_continuous_time_ = adapted.continuous_clip_time;
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
      // Cadence-only, like the adapted path: the pose is never stride-shortened.
      state_.stride_scale_ = 1.0f;
      state_.locomotion_diagnostics_.reach_limited = false;
      const LocomotionPlaybackRequest previous = state_.locomotion_playback_;
      state_.locomotion_playback_ = AdvanceLocomotionPlayback(
          previous, authority_clip, clip, achieved_speed, reference_speed,
          entry_frame_time, 0.01f, state_.stride_scale_);
      // RM-F1: the compatibility path advances the *same* continuous cursor
      // the root-driven path uses. Without this, alternating modes re-samples
      // an older phase when the root becomes executable again.
      const float playback_period = std::max(
          static_cast<float>(clip.frame_count > 1 ? clip.frame_count - 1 : 1),
          1.0f);
      state_.locomotion_continuous_time_ = AdvanceContinuousLocomotionTime(
          state_.locomotion_continuous_time_, previous.clip_id,
          authority_clip, previous.frame_time,
          state_.locomotion_playback_.frame_time, playback_period);
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
  // A raw-clip preview returns the unmodified clip skeleton; the committed
  // Final Pose is a different, realized value.
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

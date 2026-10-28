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
#include "football/player/command/player_motion.hpp"
#include "player/command/player_motion_constants.hpp"
#include "player/execution/player_runtime_sink.hpp"
#include "player/execution/player_touch_math.hpp"

using namespace football::math;
using football::ball::Ball;

namespace {
constexpr bool animSmoothing = true;
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle =
    animSmoothing ? 0.25f * pi : 0.0f;
}  // namespace


PlayerActionState Player::CaptureLegacyActionState() const {
  PlayerActionState legacy;
  legacy.type = legacy_anim_state_.functionType;
  legacy.elapsed = {static_cast<std::uint64_t>(legacy_anim_state_.frameNum)};
  legacy.duration = {static_cast<std::uint64_t>(GetFrameCount())};
  const Anim *anim = &legacy_anim_state_;
  if (anim->touchFrame >= 0)
    legacy.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  legacy.contactPosition = anim->touchPos;
  return legacy;
}

void Player::CheckSimulationActionOracle() const {
  const PlayerActionState legacy = CaptureLegacyActionState();
  const bool contactPositionMatches =
      std::memcmp(actionState.contactPosition.coords,
                  legacy.contactPosition.coords,
                  sizeof(actionState.contactPosition.coords)) == 0;
  std::string mismatch;
  if (actionState.type != legacy.type) {
    mismatch = "type";
  } else if (actionState.elapsed != legacy.elapsed) {
    mismatch = "frame";
  } else if (actionState.duration != legacy.duration) {
    mismatch = "frame count";
  } else if (actionState.contact != legacy.contact) {
    mismatch = "contact frame";
  } else if (!contactPositionMatches) {
    mismatch = "contact position bits";
  }
  assert(mismatch.empty() &&
         "authoritative action state diverged from legacy oracle");
}

void Player::BeginSimulationAction() {
  const Anim *anim = &legacy_anim_state_;
  PlayerActionDefinition definition;
  definition.type = legacy_anim_state_.functionType;
  definition.duration = {static_cast<std::uint64_t>(GetFrameCount())};
  if (anim->touchFrame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(anim->touchFrame)};
  definition.contactPosition = anim->touchPos;
  last_begun_definition_ = definition;
  last_begun_start_frame_ = legacy_anim_state_.frameNum;
  PlayerActionExecutor::Begin(actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never derive
  // executor time from the compatibility projection.
  const football::sim::TickSpan initial_elapsed{static_cast<std::uint64_t>(legacy_anim_state_.frameNum)};
  if (initial_elapsed.value > 0) {
    PlayerActionExecutor::Step(actionState, initial_elapsed);
  }
  CheckSimulationActionOracle();
}

// ANIM-1 A6.4 step 4: single Player entry point for touch-animation selection.
  // The evaluation runs against the Player's own touch math; the Player owns
  // the decision and retains the result.
int Player::SelectTouchAction(football::sim::Tick now, const football::sim::PlayerTickContext &tick,
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
    touch_choice_ = choice;
    ++touch_selection_successes_;
  }
  return anim_id;
}

bool Player::CommitSelectedAction(int selected_anim_id,
                                  const football::sim::TouchChoice &touch_choice,
                                  const PlayerCommand &command,
                                  const football::sim::PlayerTickContext &tick,
                                  football::sim::Tick now,
                                  const football::sim::player::PlayerObservation& observation,
                                  e_InterruptAnim local_interrupt_anim,
                                  bool material_candidate,
                                  int start_frame) {
  // ANIM-1 A6.4 step 7b: the Player owns the install dispatch. The compat
  // projection writes the legacy currentAnim; the Player then installs through
  // the one install entry, and only then records the scheduled-contact audit.
  const bool interrupted =
      actionState.type != e_FunctionType_Movement &&
      !actionState.IsComplete() && !actionState.IsAtLastFrame() &&
      local_interrupt_anim != e_InterruptAnim_None;
  ProjectSelectedActionCompat(selected_anim_id, touch_choice, command, tick, now,
                              observation, local_interrupt_anim, material_candidate,
                              start_frame);
  InstallSelectedAction(CurrentSelectedAction());
  action_feedback_.source_action = command.source_action;
  action_feedback_.command_status =
      command.source_action == ControlAction::None
          ? football::sim::player::CommandBuildStatus::MovementOnly
          : football::sim::player::CommandBuildStatus::Accepted;
  action_feedback_.interrupted = interrupted;
  RecordSelectedContactAudit(command);
  return true;
}

void Player::InstallSelectedAction(const football::sim::SelectedAction &selection) {
  // ANIM-1 A6.3b.3 (second cut): the Player is now the authority for the action
  // definition - it is built from the Player-owned `SelectedAction`, not read
  // back from the compatibility projection. The compat projection is synced by
  // selector before this call and is cross-checked below. The entry never
  // re-selects a clip or vetoes an interrupt.
  installed_action_ = selection;

  PlayerActionDefinition definition;
  definition.type = selection.function_type;
  definition.duration = {static_cast<std::uint64_t>(selection.frame_count)};
  if (selection.touch_frame >= 0)
    definition.contact = football::sim::TickSpan{static_cast<std::uint64_t>(selection.touch_frame)};
  definition.contactPosition = selection.touch_position;
  last_begun_definition_ = definition;
  last_begun_start_frame_ = selection.start_frame;
  PlayerActionExecutor::Begin(actionState, definition);
  if (selection.start_frame > 0) {
    PlayerActionExecutor::Step(
        actionState, football::sim::TickSpan{static_cast<std::uint64_t>(selection.start_frame)});
  }
  action_feedback_.source_action = ControlAction::None;
  action_feedback_.command_status =
      football::sim::player::CommandBuildStatus::MovementOnly;
  action_feedback_.type = selection.function_type;
  action_feedback_.installed = selection.function_type != e_FunctionType_None;
  action_feedback_.contact_due = false;
  action_feedback_.touch_prepared = false;
  action_feedback_.touch_accepted = false;
  action_feedback_.completed = false;
  action_feedback_.interrupted = false;
  action_feedback_.start_frame = selection.start_frame;
  action_feedback_.touch_frame = selection.touch_frame;
  action_feedback_.prepared_touch_tick = {};
  CheckSimulationActionOracle();

  // Oracle during migration: the compat projection must still agree with
  // the Player-owned selection, otherwise the two authorities have drifted.

  // Same-tick consistency: the committed cursor, the scheduled contact and the
  // compat projection must all agree with the selection immediately, so an
  // install followed by a sample in the same tick cannot be one tick off.
  ++install_cursor_checks_;
  const bool cursor_same =
      actionState.Frame() == selection.start_frame &&
      actionState.FrameCount() == selection.frame_count &&
      actionState.HasScheduledContact() == (selection.touch_frame >= 0) &&
      actionState.ContactFrame() == selection.touch_frame;
  if (cursor_same) {
    ++install_cursor_matches_;
  } else {
    ++install_cursor_mismatches_;
  }
  ++install_checks_;
  const Anim *anim = &legacy_anim_state_;
  const bool same =
      selection.function_type == legacy_anim_state_.functionType &&
      selection.frame_count == GetFrameCount() &&
      selection.start_frame == legacy_anim_state_.frameNum &&
      selection.touch_frame == anim->touchFrame &&
      selection.touch_position == anim->touchPos;
  if (same) {
    ++install_matches_;
  } else {
    ++install_mismatches_;
  }

}

void Player::StepSimulationAction(football::sim::TickSpan elapsed) {
  PlayerActionExecutor::Step(actionState, elapsed);
  CheckSimulationActionOracle();
}

void Player::UpdatePlayback(AnimationId clip_id, int frame, float base_angle,
                            int blend_ticks) {
  const AnimationClip& clip = animations_.Get(static_cast<std::uint32_t>(clip_id));
  playback_state_.Play(clip, frame, base_angle,
                         playback_blend_enabled_ ? blend_ticks : 0);
}

void Player::AdvanceActionTick() {
  // ANIM-1 A6.3c.2: one real time advance. The PlayerActionState cursor is the
  // authority; the compatibility integer frame is a read-only projection.
  PlayerActionExecutor::Step(actionState, football::sim::TickSpan{1});
  legacy_anim_state_.frameNum = actionState.Frame();
  action_feedback_.type = actionState.type;
  action_feedback_.contact_due =
      actionState.HasScheduledContact() && actionState.IsContactDue();
  action_feedback_.completed = actionState.IsComplete();
  CheckSimulationActionOracle();
}

const AnimationClip &Player::GetBakedClip(AnimationId id) const {
  return animations_.Get(static_cast<uint32_t>(id));
}

football::sim::SelectedAction Player::CurrentSelectedAction() const {
  football::sim::SelectedAction selection;
  selection.clip_id = legacy_anim_state_.animationId;
  selection.function_type = legacy_anim_state_.functionType;
  selection.start_frame = legacy_anim_state_.frameNum;
  selection.frame_count = static_cast<int>(GetBakedClip(legacy_anim_state_.animationId).frame_count);
  selection.touch_frame = legacy_anim_state_.touchFrame;
  selection.touch_position = legacy_anim_state_.touchPos;
  return selection;
}

const AnimationClip &Player::GetCurrentBakedClip() const {
  return GetBakedClip(legacy_anim_state_.animationId);
}

// ---------------------------------------------------------------------------
// ANIM-1 A9.5c/A9.8: the compat install projection and the pose assembly,
// relocated verbatim from Humanoid::ProjectSelectedActionCompat /
// Humanoid::RecordSelectedContactAudit and HumanoidBase::SampleBakedPose.
// ---------------------------------------------------------------------------
PlayerBakedPoseSample Player::SampleBakedPose(
    const PlayerKinematicState& state,
    const football::player::PlayerBodyProfile& profile) const {
  const auto& clip = GetCurrentBakedClip();
  if (clip.poses.empty() || legacy_anim_state_.frameNum < 0)
    throw std::logic_error("missing runtime baked pose frame");
  const auto frame = std::min(std::size_t(legacy_anim_state_.frameNum), clip.poses.size() - 1);
  PlayerBakedPoseSample sample;
  sample.pose = clip.poses[frame];
  sample.state = state;
  sample.animation_id = legacy_anim_state_.animationId;
  sample.requested_frame = static_cast<std::int64_t>(legacy_anim_state_.frameNum);
  sample.sampled_frame = static_cast<std::int64_t>(frame);
  sample.frame_clamped = sample.requested_frame != sample.sampled_frame;
  sample.base_angle = startAngle + legacy_anim_state_.rotationSmuggleOffset;
  sample.body_profile = profile;
  sample.action = legacy_anim_state_.functionType;
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
PlayerBakedPoseSample Player::PreviewRawPose(int frame_offset) const {
  PlayerBakedPoseSample sample = GetBakedPoseSample();
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
  return sample;
}

void Player::ProjectSelectedActionCompat(
    int selectedAnimID, const football::sim::TouchChoice &touch_choice,
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, const football::sim::player::PlayerObservation& observation,
    e_InterruptAnim localInterruptAnim, bool material_candidate,
    int start_frame) {
  const PlayerActionState &action = this->GetSimulationActionState();
  previousAnim_functionType = legacy_anim_state_.functionType;

  legacy_anim_state_.animationId = selectedAnimID;
  legacy_anim_state_.functionType = command.desiredFunctionType;
  // ANIM-2 A13: install at the matched entry frame. 0 is the legacy cursor.
  legacy_anim_state_.frameNum = start_frame;
  legacy_anim_state_.touchFrame = touch_choice.touch_frame;
  legacy_anim_state_.touchPos = touch_choice.touch_position;
  legacy_anim_state_.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, legacy_spatial_state_.relBodyAngleNonquantized - GetCurrentBakedClip().metadata.incoming_body_angle) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (legacy_anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (legacy_anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
  legacy_anim_state_.rotationSmuggle.end = touch_choice.rotation_smuggle;
  legacy_anim_state_.rotationSmuggleOffset = 0;
  legacy_anim_state_.actionSmuggle = touch_choice.action_smuggle;
  legacy_anim_state_.actionSmuggleOffset = Vector3(0);
  legacy_anim_state_.actionSmuggleSustain = Vector3(0); // calculated below
  legacy_anim_state_.actionSmuggleSustainOffset = Vector3(0);
  legacy_anim_state_.movementSmuggle = Vector3(0); // needs to be reset here, else the previous calc is used in upcoming 'calculatemovementsmuggle'
  legacy_anim_state_.movementSmuggleOffset = Vector3(0);
  legacy_anim_state_.incomingMovement = legacy_spatial_state_.movement;
  legacy_anim_state_.outgoingMovement = CalculateOutgoingMovement(touch_choice.positions);
  legacy_anim_state_.positions.clear();
  legacy_anim_state_.positions.assign(touch_choice.positions.begin(), touch_choice.positions.end());
  // ANIM-2 A13: a non-zero entry frame skips the clip's earlier root motion, so
  // re-base the reference trajectory to the entry frame. Otherwise the first
  // committed tick would add the whole skipped displacement as a teleport. A
  // constant shift leaves every delta (outgoing movement, smuggles) unchanged.
  if (start_frame > 0 && !legacy_anim_state_.positions.empty()) {
    const std::size_t index =
        std::min(static_cast<std::size_t>(start_frame),
                 legacy_anim_state_.positions.size() - 1);
    const Vector3 base = legacy_anim_state_.positions[index];
    for (Vector3 &p : legacy_anim_state_.positions) p -= base;
  }
  legacy_anim_state_.positionOffset = Vector3(0);
  legacy_anim_state_.originatingCommand = command;
  RecordMovementCommandAcceptance(material_candidate, action.type,
                                  static_cast<int>(football::sim::ToMilliseconds(action.elapsed)), localInterruptAnim,
                                  command, static_cast<int>(GetCurrentBakedClip().frame_count));
  // Must run after the legacy_anim_state_ field writes above (it reads the new
  // animationId/positions/touchFrame and GetCurrentBakedClip), before install.
  legacy_anim_state_.movementSmuggle = this->ComputeMovementSmuggle(now, tick, command.desiredDirection, command.desiredVelocityFloat, observation);
  legacy_anim_state_.movementSmuggleOffset = Vector3(0);
}

void Player::RecordSelectedContactAudit(const PlayerCommand &command) {
  const PlayerActionState &scheduled = this->GetSimulationActionState();
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

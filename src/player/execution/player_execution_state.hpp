// PEX-1/PEX-2: the execution process state, as one owned value.
//
// Player is the player entity: identity, capability, match state and the
// authoritative kinematic state. Everything between an accepted Control / Play
// / React input and the pose that is drawn is *execution process state*: which
// action is installed, how far its clock has run, what the animation playback
// is doing, which inputs are pending, which scheduler/epoch is current and what
// the legacy compatibility projection last looked like.
//
// All of it lives here, in exactly one copy. Nothing in this struct is a second
// authority for kinematics: `legacy_spatial_state_` is a projection of the
// committed PlayerKinematicState, and `actionState` is the only action timeline
// (the legacy Anim::frameNum mirror is projected from it).
//
// Groups, by owner-visible purpose (PEX-1 field attribution):
//   inputs      pending Control / Play / React and their recorded outcomes
//   action      the installed action and its single timeline cursor
//   playback    animation playback, final pose and the locomotion animation feed
//   selection   the last touch-animation selection result
//   scheduling  Player Decision Clock, locomotion intent cadence and epochs
//   compat      legacy animation/spatial projection used by the baked action math
//
// Measurement-only provenance and counters live in a separate
// PlayerExecutionAuditState (see below); they never decide gameplay.
#ifndef FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP
#define FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP

#include <chrono>
#include <optional>
#include <vector>

#include "football/animation/playback.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_action.hpp"
#include "football/player/player_action_request.hpp"
#include "football/player/execution/player_action_adapter.hpp"
#include "football/player/execution/player_foot_ik.hpp"
#include "player/execution/locomotion_intent_scheduler.hpp"
#include "player/execution/player_action_execution_state.hpp"
#include "player/execution/player_action_executor.hpp"
#include "player/execution/player_action_feedback.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_decision_scheduler.hpp"
#include "player/execution/player_interrupt.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/player_selected_action.hpp"
#include "player/execution/player_touch_choice.hpp"
#include "player/player_spatial_state.hpp"

// 4f-a3a: serialized gameplay authority for the complete controller decision.
struct PlayerDecisionQueueState {
  PlayerCommandQueue commands;
  bool initialized = false;
};

// H3e4f-g0b-decision-intent: the serialized Player Decision Clock locomotion
// state. Since the execution authority flip this is the sole command source for
// pure-locomotion execution: the gate requires a current-epoch publication, and
// both PlayerLocomotion and PlayerBodyFacing consume this command.
struct PlayerDecisionLocomotionState {
  PlayerCommand command;
  bool initialized = false;
  // H3e4f-g0b-continuity-epoch: validity is a continuity property, not a time
  // one. The epoch advances when the world leaves locomotion continuity, so an
  // intent published in an earlier epoch must not drive the new one. These two
  // epochs decide whether a continuity repair queries the controller, so they
  // are gameplay state and are serialized.
  unsigned long long continuityEpoch = 0;
  unsigned long long publishedEpoch = 0;
  // Continuity tracking. Gameplay, not audit: leaving eligibility advances the
  // epoch.
  bool wasEligibleLastTick = false;
  bool continuityStarted = false;
};

// ANIM-1 A7: a passive trip the Player accepted at contact time. It is applied
// to the animation interrupt at the next action tick, which is exactly when the
// legacy direct write took effect. Gameplay state, cleared by resets.
struct PendingPassiveTrip {
  Vector3 direction{0};
  int trip_type = 0;
};

struct PlayerExecutionState {
  // ---------------------------------------------------------------------
  // inputs: the pending-input boundary and its recorded arbitration outcome
  // ---------------------------------------------------------------------
  std::optional<PlayerControl> control_;
  std::optional<PlayerActionRequest> pending_action_request_;
  std::optional<PlayerReaction> pending_reaction_;
  std::optional<PendingPassiveTrip> pending_passive_trip_;
  RequestOutcome action_request_outcome_ = RequestOutcome::Accepted;
  RequestOutcome reaction_outcome_ = RequestOutcome::Accepted;

  // ---------------------------------------------------------------------
  // action: the single action timeline and the installed selection
  // ---------------------------------------------------------------------
  PlayerActionState actionState;
  football::sim::SelectedAction installed_action_;
  PlayerActionFeedback action_feedback_;
  // The definition the executor actually committed; oracle-only.
  PlayerActionDefinition last_begun_definition_;
  int last_begun_start_frame_ = 0;

  // ---------------------------------------------------------------------
  // playback: animation playback, final pose, locomotion animation feed
  // ---------------------------------------------------------------------
  football::animation::PlaybackState playback_state_;
  // P3/M3: the single Final Pose channel. `playback_state_.final_pose` is the
  // animation's blended output; this is the Player-corrected skeleton (warp ->
  // bounded realization -> stride/plant IK) that Pose(), the baked sample, the
  // colliders and the touch geometry all read. The base yaw stays separate so
  // animation matching still sees local joint rotations.
  PoseFrame final_pose_;
  float final_pose_base_angle_ = 0.0f;
  // The clip time the Final Pose was settled from. A post-tick re-settle must
  // reuse it, not re-derive one from the integer action cursor: a locomotion
  // pose lives at a fractional clip time.
  float settle_clip_frame_time_ = 0.0f;
  bool final_pose_valid_ = false;
  // The realized pose the Final Pose was settled from. A root correction after
  // the tick re-settles this pose instead of re-deriving one.
  PoseFrame settle_source_pose_;
  bool settle_source_valid_ = false;
  // ANIM-2 A10.1: the tick of the last real playback update, so a repeated
  // same-tick call cannot advance the transition twice.
  std::optional<football::sim::Tick> last_playback_tick_;
  bool playback_blend_enabled_ = true;
  // P3b: support-foot world anchors. Only Movement clips populate this.
  PlayerFootPlantState foot_plant_;
  // Support detection state: the previous tick's hip-relative fore-aft ankle
  // offsets, plus which foot was support. A foot travelling backwards relative
  // to the body is the one carrying weight (see SelectFootSupport).
  float previous_fore_aft_[2] = {0.0f, 0.0f};
  // World ankle positions of the previous tick's gait pose, used to check that a
  // foot the clip calls "stance" is really standing still in the world.
  football::math::Vector3 previous_world_ankle_[2] = {
      football::math::Vector3(0), football::math::Vector3(0)};
  bool have_previous_fore_aft_ = false;
  bool support_left_ = false;
  bool have_support_ = false;
  // LOCO-2.3b: fresh snapshot of the last committed locomotion tick.
  std::optional<LocomotionAnimationInput> locomotion_animation_input_;
  // M2: the motion-driven playback cursor. Carried across ticks so the gait
  // phase stays continuous while the same clip keeps playing.
  LocomotionPlaybackRequest locomotion_playback_;
  // M3: the clip's own reference displacement for this tick, in clip-local XY.
  // The warp compares it (rotated by the base yaw) with the achieved motion.
  football::math::Vector3 locomotion_reference_delta_ =
      football::math::Vector3(0);
  // STRIDE-1: smoothed stride scale applied to the gait's fore-aft excursion.
  // Paired with the playback rate, which divides by the same value.
  float stride_scale_ = 1.0f;
  // STRIDE-0: measured motion/animation agreement for the last locomotion tick.
  LocomotionPoseDiagnostics locomotion_diagnostics_;
  // RMA-2/RMA-3: the unified adaptation result for this tick's ordinary
  // locomotion. Motion commit and pose playback both read this one sample, so
  // the root and the pose cannot be derived from different phases.
  AdaptedMotionSample adapted_locomotion_;
  bool adapted_locomotion_valid_ = false;
  bool locomotion_animation_adaptation_ = true;

  // ---------------------------------------------------------------------
  // selection: the retained touch-animation selection result
  // ---------------------------------------------------------------------
  football::sim::TouchChoice touch_choice_;

  // ---------------------------------------------------------------------
  // scheduling: decision clock, locomotion cadence, continuity epochs
  // ---------------------------------------------------------------------
  LocomotionIntentScheduler locomotionIntentScheduler;
  PlayerDecisionScheduler playerDecisionScheduler;
  PlayerDecisionQueueState playerDecisionQueue;
  // The sole locomotion intent of the Player Decision Clock.
  PlayerDecisionLocomotionState decisionLocomotionState;

  // ---------------------------------------------------------------------
  // compat: the legacy animation/spatial projection (never an authority)
  // ---------------------------------------------------------------------
  Anim legacy_anim_state_;
  SpatialState legacy_spatial_state_;
  bool legacy_state_initialized_ = false;
  e_FunctionType previousAnim_functionType = e_FunctionType_None;
  Vector3 startPos;
  radian startAngle = 0;
  Vector3 nextStartPos;
  radian nextStartAngle = 0;
  Vector3 previousPosition2D;
  e_InterruptAnim interruptAnim = e_InterruptAnim_None;
  int reQueueDelayFrames = 0;
  int tripType = 0;
  Vector3 tripDirection;
  Vector3 decayingPositionOffset;
  float decayingDifficultyFactor = 0.0f;
  // ANIM-2 A10.3c: the resolved movement-similarity request. It is only
  // written when a command carries desired movement and is read by the retain
  // clip sort, exactly as the legacy SetMovementSimilarityPredicate left it.
  mutable Vector3 predicate_RelDesiredDirection;
  mutable float predicate_CorneringBias = 0.0f;
  mutable e_Velocity predicate_DesiredVelocity = e_Velocity_Idle;
  std::chrono::milliseconds mentalImageTime{0};

};

// PEX-5: measurement-only execution counters. None of them decides a
// selection, a motion, an action cursor or a publication; the gameplay
// epochs, schedulers and cursors stay in PlayerExecutionState. Keeping them
// in one separate value makes the audit provably a side channel: removing a
// counter here cannot change gameplay.
struct PlayerExecutionAuditState {
  // --- execution oracle and request counters ---------------------------
  int install_checks_ = 0;
  int install_matches_ = 0;
  int install_mismatches_ = 0;
  int install_cursor_checks_ = 0;
  int install_cursor_matches_ = 0;
  int install_cursor_mismatches_ = 0;
  int authority_checks_ = 0;
  int authority_matches_ = 0;
  int authority_mismatches_ = 0;
  int action_request_attempts_ = 0;
  int action_request_accepts_ = 0;
  int action_request_defers_ = 0;
  int action_request_rejects_ = 0;
  int action_request_build_failures_ = 0;
  int action_request_inputs_consumed_ = 0;
  int reaction_trip_interrupts_scheduled_ = 0;
  int action_request_preemptions_ = 0;
  int touch_selection_successes_ = 0;
  // M4: how often a root corrected after the tick forced a bounded re-settle.
  int pose_resolves_ = 0;
  // P1: how often an unreachable support anchor was released.
  int plant_releases_ = 0;
};

#endif  // FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP

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
//   selection   the last touch-animation selection result and its counters
//   scheduling  Player Decision Clock, locomotion intent cadence and epochs
//   compat      legacy animation/spatial projection used by the baked action math
//   diagnostics audit counters that measure the migration, never gameplay
#ifndef FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP
#define FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP

#include <chrono>
#include <optional>
#include <vector>

#include "football/animation/playback.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_action.hpp"
#include "football/player/player_action_request.hpp"
#include "player/execution/locomotion_intent_scheduler.hpp"
#include "player/execution/player_action_execution_state.hpp"
#include "player/execution/player_action_executor.hpp"
#include "player/execution/player_action_feedback.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_decision_scheduler.hpp"
#include "player/execution/player_execution_diagnostics.hpp"
#include "player/execution/player_interrupt.hpp"
#include "player/execution/player_locomotion_animation.hpp"
#include "player/execution/player_selected_action.hpp"
#include "player/execution/player_touch_choice.hpp"
#include "player/player_spatial_state.hpp"

// 4f-a3a: serialized gameplay authority for the complete controller decision.
struct PlayerDecisionQueueState {
  PlayerCommandQueue commands;
  bool initialized = false;
  unsigned long long generation = 0;
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
  // ANIM-2 A10.1: the tick of the last real playback update, so a repeated
  // same-tick call cannot advance the transition twice.
  std::optional<football::sim::Tick> last_playback_tick_;
  bool playback_blend_enabled_ = true;
  // LOCO-2.3b: fresh snapshot of the last committed locomotion tick.
  std::optional<LocomotionAnimationInput> locomotion_animation_input_;
  bool locomotion_animation_adaptation_ = true;

  // ---------------------------------------------------------------------
  // selection: the retained touch-animation selection result
  // ---------------------------------------------------------------------
  football::sim::TouchChoice touch_choice_;
  int touch_selection_successes_ = 0;

  // ---------------------------------------------------------------------
  // scheduling: decision clock, locomotion cadence, continuity epochs
  // ---------------------------------------------------------------------
  LocomotionIntentScheduler locomotionIntentScheduler;
  bool locomotionIntentDueThisTick = false;
  // Explicit cause of the next Direct publication. A continuity repair runs
  // before the cadence note, so deriving the cause from that flag would
  // mislabel a repair publication as an animation-driven one.
  int pendingPublicationCause = 0;
  // 4f-a1: transient, per-tick animation selection outcome. Never serialized.
  bool decisionMovementSelection = false;
  PlayerDecisionScheduler playerDecisionScheduler;
  PlayerDecisionQueueState playerDecisionQueue;
  // The sole locomotion intent of the Player Decision Clock.
  PlayerDecisionLocomotionState decisionLocomotionState;
  std::optional<football::sim::Tick> last_locomotion_publication_tick_;
  std::optional<football::sim::Tick> last_reset_tick_;
  // Whether a real action tick has run since the last reset. Player Decision
  // re-entry classification uses it, so it is execution bookkeeping.
  bool hasProcessedPlayerTick = false;

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

  // ---------------------------------------------------------------------
  // diagnostics: migration/audit measurements, never gameplay
  // ---------------------------------------------------------------------
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
  // Audit generation: bumped on every Direct publication for re-entry freshness
  // measurement. Deliberately transient so telemetry cannot change the
  // save-state contract; the gameplay epochs above are serialized instead.
  unsigned long long decisionLocomotionAuditGeneration = 0;
  // Re-entry provenance. Transient, never serialized, measurement only.
  int reentryAuditEpoch = -1;
  bool locomotionExitRecorded = false;
  bool resetGenerationAnchorValid = false;
  unsigned long long resetDecisionGeneration = 0;
  bool reentryAuditStarted = false;
  bool wasPureLocomotionLastTick = false;
  bool resetSinceLastPlayerTick = false;
  bool lastPublicationViaSimulationCadence = false;
  bool lastPublicationWhileIneligible = false;
  unsigned long long decisionGenerationAtLocomotionExit = 0;
  // Caller scope of the current ResetSituation, so the reset provenance keeps
  // distinguishing the initial, runtime and Deactivate resets.
  int reset_situation_audit_context = kResetSituationUnspecified;
};

#endif  // FOOTBALL_SIM_PLAYER_EXECUTION_STATE_HPP

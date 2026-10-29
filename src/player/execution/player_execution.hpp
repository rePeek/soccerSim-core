// PEX-3: PlayerExecution owns the execution process, not just its storage.
//
// It consumes Control / Play / React, advances the action timeline and the
// animation playback it selected, and answers the Player's read-only
// observation queries. The Player still calls it: Runtime never sees this type.
//
// Ownership boundaries this class enforces:
//   - it owns the single PlayerExecutionState (action cursor, playback, pending
//     inputs, scheduling/epochs, legacy compatibility projection);
//   - it does not own the kinematic state, the body profile, the touch record,
//     possession facts, cards or the formation entry - those are read through
//     the PlayerExecutionPort for the duration of a call;
//   - it never stores the port or a Player*, so it cannot reach back into the
//     entity outside a call it was given.
#ifndef FOOTBALL_SIM_PLAYER_EXECUTION_HPP
#define FOOTBALL_SIM_PLAYER_EXECUTION_HPP

#include <optional>

#include "football/animation/types.hpp"
#include "football/player/player_control.hpp"
#include "player/execution/player_action_feedback.hpp"
#include "player/execution/player_baked_pose_sample.hpp"
#include "player/execution/player_command.hpp"
#include "player/execution/player_execution_port.hpp"
#include "player/execution/player_execution_state.hpp"
#include "player/execution/player_selection_decisions.hpp"
#include "player/execution/player_tick_context.hpp"
#include "player/execution/player_touch_choice.hpp"
#include "player/player_observation.hpp"

namespace football::sim {
class PlayerRuntimeSink;
}
namespace football::sim::player {
class PlayerTouchSink;
}
namespace football::ball {
class Ball;
}
class AnimationLibrary;
struct AnimationClip;
struct CrudeSelectionQuery;
struct PlayerCommandInputs;
struct CommandBuildResult;
struct LocomotionAnimationInput;

class PlayerExecution final {
 public:
  // Read-only, lifetime-stable dependency. Everything else arrives per call.
  explicit PlayerExecution(const AnimationLibrary& animations);
  ~PlayerExecution();

  PlayerExecution(const PlayerExecution&) = delete;
  PlayerExecution& operator=(const PlayerExecution&) = delete;

  // --- control input ----------------------------------------------------
  void SetControl(PlayerExecutionPort& owner, const PlayerControl& control);
  void ClearControl(PlayerExecutionPort& owner);
  float ControlSpeed() const;

  // --- discrete requests and reactions ----------------------------------
  RequestOutcome Play(PlayerExecutionPort& owner, const PlayerActionRequest& request);
  RequestOutcome React(PlayerExecutionPort& owner, const PlayerReaction& reaction,
                       const Player* ball_retainer);
  const std::optional<PlayerActionRequest>& PendingActionRequest() const {
    return state_.pending_action_request_;
  }
  const std::optional<PlayerReaction>& PendingReaction() const {
    return state_.pending_reaction_;
  }
  RequestOutcome LastActionRequestOutcome() const { return state_.action_request_outcome_; }
  RequestOutcome LastReactionOutcome() const { return state_.reaction_outcome_; }
  int ActionRequestAttempts() const { return audit_.action_request_attempts_; }
  int ActionRequestAccepts() const { return audit_.action_request_accepts_; }
  int ActionRequestDefers() const { return audit_.action_request_defers_; }
  int ActionRequestRejects() const { return audit_.action_request_rejects_; }
  int ActionRequestBuildFailures() const {
    return audit_.action_request_build_failures_;
  }
  int ActionRequestInputsConsumed() const {
    return audit_.action_request_inputs_consumed_;
  }
  int ReactionTripInterruptsScheduled() const {
    return audit_.reaction_trip_interrupts_scheduled_;
  }
  int ActionRequestPreemptions() const {
    return audit_.action_request_preemptions_;
  }

  // --- lifecycle --------------------------------------------------------
  // Fresh actor state. `spawn_position` is the formation placement the Player
  // computed; execution owns the reset sequence that re-reads the idle clip.
  void Activate(PlayerExecutionPort& owner, const Vector3& spawn_position);
  // Reset before leaving play. The double reset and its epoch effect are kept.
  void Deactivate(PlayerExecutionPort& owner, football::sim::Tick now);
  // Teardown reset. No roster callbacks, no hidden clock path.
  void Exit(PlayerExecutionPort& owner, football::sim::Tick now);
  void ResetPosition(PlayerExecutionPort& owner, const Vector3& new_position,
                     const Vector3& focus_position);
  // The Player committed a position correction (contact or the goalkeeper
  // boundary clamps). The compatibility projection adopts the same offset
  // without re-deriving the state the Player just installed.
  void ApplyCompatibilityPositionOffset(const Vector3& offset) {
    ApplyLegacyPositionOffset(offset);
  }
  void ResetSituation(PlayerExecutionPort& owner, const Vector3& focus_position,
                      football::sim::Tick now);
  void SelectRetainAnim(PlayerExecutionPort& owner);
  // Retain-clip re-selection without a new borrow (the caller already holds one).
  void SelectRetainAnim();

  // Pure decision: is an extreme (last-ditch) animation justified? The team
  // possession weight is an input, not cached state; the time-to-ball values
  // come from the Player's own capability estimate.
  bool AllowLastDitch(const football::player::PlayerBallRelation& ball_relation,
                      float team_possession_amount,
                      bool include_possession_amount = true) const;

  // --- tick -------------------------------------------------------------
  void RunActionTick(PlayerExecutionPort& owner, football::sim::Tick now,
                     const football::sim::PlayerTickContext& tick,
                     const football::sim::player::PlayerObservation& observation,
                     football::sim::player::PlayerTouchSink& touch_sink,
                     football::sim::PlayerRuntimeSink& runtime_sink);
  // Fatal in all builds: the action cursor, the installed selection and the
  // compatibility projection must agree at every sync point.
  void CheckSimulationActionOracle() const;
  bool IsEligibleForProceduralLocomotion(bool retaining_ball) const;
  // LOCO-2.3b: the Player's CommitLocomotion hands the committed tick to the
  // animation adapter through here; only the executor consumes it.
  void RecordLocomotionAnimationInput(const LocomotionAnimationInput& input) {
    state_.locomotion_animation_input_ = input;
  }

  // --- observation ------------------------------------------------------
  const PlayerActionState& ActionState() const { return state_.actionState; }
  const football::sim::SelectedAction& InstalledAction() const {
    return state_.installed_action_;
  }
  const PlayerActionFeedback& Feedback() const { return state_.action_feedback_; }
  const football::animation::PlaybackState& Playback() const {
    return state_.playback_state_;
  }
  const PoseFrame& PlaybackPose() const { return state_.playback_state_.final_pose; }
  float PlaybackBaseAngle() const { return state_.playback_state_.final_base_angle; }
  PoseFrame Pose(PlayerExecutionPort& owner) const;
  PlayerBakedPoseSample BakedPoseSample(PlayerExecutionPort& owner) const;
  PlayerBakedPoseSample PreviewRawPose(PlayerExecutionPort& owner, int frame_offset) const;
  const football::sim::TouchChoice& TouchSelection() const { return state_.touch_choice_; }
  int FrameNum() const { return state_.legacy_anim_state_.frameNum; }
  int FrameCount() const;
  AnimationId CurrentAnimationId() const {
    return state_.legacy_anim_state_.animationId;
  }
  radian RelBodyAngle() const { return state_.legacy_spatial_state_.relBodyAngleNonquantized; }
  e_Velocity EnumVelocity() const { return state_.legacy_spatial_state_.enumVelocity; }
  float FloatVelocity() const {
    return EnumToFloatVelocity(state_.legacy_spatial_state_.enumVelocity);
  }
  e_FunctionType PreviousFunctionType() const { return state_.previousAnim_functionType; }
  const std::optional<LocomotionAnimationInput>& LocomotionAnimationInputSnapshot() const {
    return state_.locomotion_animation_input_;
  }
  bool LocomotionAnimationAdaptation() const {
    return state_.locomotion_animation_adaptation_;
  }
  void SetLocomotionAnimationAdaptation(bool enabled) {
    state_.locomotion_animation_adaptation_ = enabled;
  }
  void SetMovementPoseBlend(bool enabled) { state_.playback_blend_enabled_ = enabled; }
  bool MovementPoseBlend() const { return state_.playback_blend_enabled_; }
  const Vector3& DecayingPositionOffset() const { return state_.decayingPositionOffset; }
  float DecayingPositionOffsetLength() const {
    return state_.decayingPositionOffset.GetLength();
  }
  int AuthorityChecks() const { return audit_.authority_checks_; }
  int AuthorityMatches() const { return audit_.authority_matches_; }
  int AuthorityMismatches() const { return audit_.authority_mismatches_; }
  int InstallChecks() const { return audit_.install_checks_; }
  int InstallMatches() const { return audit_.install_matches_; }
  int InstallMismatches() const { return audit_.install_mismatches_; }
  int InstallCursorChecks() const { return audit_.install_cursor_checks_; }
  int InstallCursorMatches() const { return audit_.install_cursor_matches_; }
  int InstallCursorMismatches() const { return audit_.install_cursor_mismatches_; }
  int TouchSelectionSuccesses() const { return audit_.touch_selection_successes_; }

  // The compatibility projection, as the values the Player's kinematic oracle
  // compares against the committed state.
  struct Projection {
    Vector3 position;
    Vector3 movement;
    Vector3 body_facing;
  };
  Projection CompatibilityProjection() const;
  Vector3 CompatibilityPosition() const { return state_.legacy_spatial_state_.position; }


 private:
  // Binds the owning Player for the duration of one public call. Nested calls
  // restore the previous binding, so a port implementation may re-enter a
  // non-owning entry point (e.g. recording the locomotion animation input).
  class PortScope {
   public:
    PortScope(const PlayerExecution& execution, PlayerExecutionPort& owner)
        : execution_(execution), previous_(execution.port_) {
      execution_.port_ = &owner;
    }
    ~PortScope() { execution_.port_ = previous_; }
    PortScope(const PortScope&) = delete;
    PortScope& operator=(const PortScope&) = delete;

   private:
    const PlayerExecution& execution_;
    PlayerExecutionPort* previous_;
  };

  PlayerExecutionPort& port() const { return *port_; }
  // Exact identity of the actor, for pointer-keyed consumers and comparisons
  // such as `ball_retainer == self()`. Never stored.
  Player* self() const { return &port().Actor(); }

  // --- action selection gates -------------------------------------------
  // ANIM-1 A6.4 step 6: pure decisions. They never search candidates, draw
  // RNG or mutate state.
  bool CanAttemptActionSelection(const football::sim::RequeueGateContext& context) const {
    return football::sim::AttemptActionSelectionAllowed(context);
  }
  bool CanCommitActionSelection(const football::sim::CommitSelectionContext& context) const {
    return football::sim::CommitActionSelectionAllowed(context);
  }

  // --- action tick orchestration ----------------------------------------
  struct ActionTickFrame;
  void ConsumeActionInputs(ActionTickFrame& frame);
  void BeginActionTick(ActionTickFrame& frame);
  void RefreshDecisionQueue(ActionTickFrame& frame);
  void AdvanceActionAndMovement(ActionTickFrame& frame);
  void EvaluateActionRequeue(ActionTickFrame& frame);
  void SelectActionForFrame(ActionTickFrame& frame);
  void PublishCadenceLocomotion(ActionTickFrame& frame);
  void ApplyPostActionTick(ActionTickFrame& frame);

  // --- action timeline and playback -------------------------------------
  void UpdatePlayback(AnimationId clip_id, int frame, float base_angle, int blend_ticks);
  void UpdatePlaybackFromAuthority(football::sim::Tick now, float base_angle, int blend_ticks);
  void BeginSimulationAction();
  void InstallSelectedAction(const football::sim::SelectedAction& selection);
  bool CommitSelectedAction(int selected_anim_id,
                            const football::sim::TouchChoice& touch_choice,
                            const PlayerCommand& command,
                            const football::sim::PlayerTickContext& tick,
                            football::sim::Tick now,
                            const football::sim::player::PlayerObservation& observation,
                            e_InterruptAnim local_interrupt_anim,
                            // ANIM-2 A13: the matched entry frame (0 = legacy).
                            int start_frame);
  void StepSimulationAction(football::sim::TickSpan elapsed);
  void AdvanceActionTick();
  PlayerActionState CaptureLegacyActionState() const;
  football::sim::SelectedAction CurrentSelectedAction() const;

  // --- selection --------------------------------------------------------
  int SelectTouchAction(football::sim::Tick now,
                        const football::sim::PlayerTickContext& tick,
                        const football::sim::player::PlayerObservation& observation,
                        const DataSet& sorted_data_set, bool use_desired_movement,
                        const Vector3& desired_direction, float desired_velocity_float,
                        bool use_desired_body_direction,
                        const Vector3& desired_body_direction_rel,
                        football::sim::TouchChoice& choice, e_InterruptAnim local_interrupt_anim,
                        bool prefer_pass_and_shot, bool allow_pre_touch_rotation_smuggle,
                        bool use_continuous_ball_check);
  int SelectBestTouchAnim(football::sim::Tick now,
                          const football::sim::PlayerTickContext& tick,
                          const football::sim::player::PlayerObservation& observation,
                          const DataSet& sorted_data_set, bool use_desired_movement,
                          const Vector3& desired_direction, float desired_velocity_float,
                          bool use_desired_body_direction,
                          const Vector3& desired_body_direction_rel,
                          football::sim::TouchChoice& choice, e_InterruptAnim local_interrupt_anim,
                          bool prefer_pass_and_shot, bool allow_pre_touch_rotation_smuggle,
                          bool use_continuous_ball_check);
  bool CanCommitMatchedSelection(int selected_anim_id, const PlayerCommand& command,
                                 e_InterruptAnim local_interrupt_anim,
                                 const football::sim::TouchChoice& touch_choice) const;
  CrudeSelectionQuery BuildCrudeSelectionQuery(
      const PlayerCommand& command, const football::sim::PlayerTickContext& tick,
      football::sim::Tick now, const football::sim::player::PlayerObservation& observation);
  bool NeedTouch(football::sim::Tick now, const football::sim::PlayerTickContext& tick,
                 int anim_id, const PlayerCommand& command,
                 const football::sim::player::PlayerObservation& observation);
  bool TrySelectAction(football::sim::Tick now, const football::sim::PlayerTickContext& tick,
                       const PlayerCommand& command,
                       const football::sim::player::PlayerObservation& observation,
                       e_InterruptAnim local_interrupt_anim, bool prefer_pass_and_shot,
                       bool allow_pre_touch_rotation_smuggle, bool use_continuous_ball_check);
  void BuildTripSelectionCommands(PlayerCommandQueue& queue, const Vector3& trip_direction,
                                  int trip_type, float current_velocity_float,
                                  const Vector3& direction_vec,
                                  const Vector3& position) const;
  void ComputeAndCommitActionMotion();
  Vector3 ComputeMovementSmuggle(football::sim::Tick now,
                                 const football::sim::PlayerTickContext& tick,
                                 const football::sim::player::PlayerObservation& observation);
  void ApplyFactualSpatialState();
  void ApplyGoalkeeperBoundaryClamps(const football::sim::PlayerTickContext& tick,
                                     const Player* ball_retainer);
  void ProjectSelectedActionCompat(int selected_anim_id,
                                   const football::sim::TouchChoice& touch_choice,
                                   const PlayerCommand& command,
                                   const football::sim::PlayerTickContext& tick,
                                   football::sim::Tick now,
                                   const football::sim::player::PlayerObservation& observation,
                                   int start_frame);
  void RecordSelectedContactAudit(const PlayerCommand& command);

  // --- touch publication ------------------------------------------------
  Player* ProcessActionTouches(football::sim::Tick now,
                               const football::sim::PlayerTickContext& tick,
                               Player* ball_retainer,
                               football::sim::player::PlayerTouchSink& touch_sink,
                               football::sim::PlayerRuntimeSink& runtime_sink);
  Vector3 GetBestPossibleTouch(football::sim::Tick now,
                               const football::sim::PlayerTickContext& tick,
                               const Vector3& desired_touch, e_FunctionType function_type);
  bool HighOrBouncyBall(const football::ball::Ball& ball) const;

  // --- legacy compatibility projection and locomotion -------------------
  void CalculatePredictedSituation(Vector3& predicted_position, radian& predicted_angle);
  Vector3 CalculateOutgoingMovement(const std::vector<Vector3>& positions) const;
  Vector3 CalculatePhysicsVector(football::sim::Tick now, int anim_id, bool use_desired_movement,
                                 const Vector3& desired_movement, bool use_desired_body_direction,
                                 const Vector3& desired_body_direction_rel,
                                 std::vector<Vector3>& positions_ret,
                                 radian& rotation_offset_ret) const;
  void CalculateSpatialState(const Player* ball_retainer);
  void ProjectSimulationMovementState(const PlayerKinematicState& state,
                                      const Player* ball_retainer);
  void ApplySimulationMovementState(const PlayerKinematicState& state,
                                    const Player* ball_retainer);
  void ApplySimulationBodyState(const PlayerKinematicState& state);
  void ProjectMovementState(const PlayerKinematicState& tick_start_state,
                            const Player* ball_retainer, bool restart_needs_simulation);
  bool UsesProceduralLocomotion(const Player* ball_retainer) const;
  void ResetLegacyPosition(const Vector3& new_position, const Vector3& focus_position);
  void ResetLegacySituation(const Vector3& focus_position);
  void ApplyLegacyPositionOffset(const Vector3& offset);
  void ResetRuntimeState(const Vector3& focus_position, football::sim::Tick now);

  // --- decision clock and scheduling ------------------------------------
  CommandBuildResult RequestCommand(const PlayerCommandInputs& inputs);
  CommandBuildResult BuildCommandQueueForRequest(
      const PlayerActionRequest& request, const football::sim::PlayerTickContext& tick);
  bool StagePassiveTrip(const PlayerReaction& reaction, const Player* ball_retainer);
  std::optional<PendingPassiveTrip> ConsumePendingPassiveTrip();
  std::optional<PlayerReaction> ConsumePendingReaction();
  std::optional<PlayerActionRequest> ConsumePendingActionRequest();
  bool NoteLocomotionIntentCadence(football::sim::Tick now, bool retaining_ball);
  void CommitLocomotionIntentRefresh(const football::ball::Ball& ball, football::sim::Tick now,
                                     bool retaining_ball);
  void PublishDecisionLocomotionIntent(const PlayerCommand& command);
  bool PublishMovementIntentFromQueue(const PlayerCommandQueue& queue);
  void PublishPlayerDecisionQueue(const PlayerCommandQueue& commands, football::sim::Tick now);
  bool HasPlayerDecisionQueue() const { return state_.playerDecisionQueue.initialized; }
  const PlayerCommandQueue& GetPlayerDecisionQueue() const {
    return state_.playerDecisionQueue.commands;
  }
  bool IsPlayerDecisionRefreshDue(football::sim::Tick now,
                                  football::sim::TickSpan cadence) const {
    return state_.playerDecisionScheduler.Due(now, cadence);
  }
  void AdvanceLocomotionContinuity(bool eligible);
  bool DecisionLocomotionEpochIsStale() const {
    return state_.decisionLocomotionState.publishedEpoch !=
           state_.decisionLocomotionState.continuityEpoch;
  }
  bool HasExecutableDecisionLocomotionIntent() const {
    return state_.decisionLocomotionState.initialized &&
           state_.decisionLocomotionState.command.useDesiredMovement &&
           state_.decisionLocomotionState.publishedEpoch ==
               state_.decisionLocomotionState.continuityEpoch;
  }
  const PlayerCommand& GetDecisionLocomotionIntent() const {
    return state_.decisionLocomotionState.command;
  }
  void CheckDecisionLocomotionIntentOracle() const;

  // --- baked clip access ------------------------------------------------
  const AnimationClip& GetBakedClip(AnimationId id) const;
  const AnimationClip& GetCurrentBakedClip() const;
  PlayerBakedPoseSample SampleBakedPose(const PlayerKinematicState& state,
                                        const football::player::PlayerBodyProfile& profile) const;

  PlayerExecutionState state_;
  // Measurement-only: see PlayerExecutionState for the split. Never consulted
  // by a gameplay decision.
  PlayerExecutionAuditState audit_;
  const AnimationLibrary& animations_;
  // Per-call borrow. Null outside a public entry point; never inspected while
  // null, so no call path can observe a stale owner.
  mutable PlayerExecutionPort* port_ = nullptr;
};

#endif  // FOOTBALL_SIM_PLAYER_EXECUTION_HPP

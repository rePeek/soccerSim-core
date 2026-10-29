// PEX-3: the execution lifecycle and the per-tick action orchestration.
//
// This is the component that used to be Player::RunActionTick and its phase
// methods. The bodies are the same movements in the same order; the receiver is
// the executor, the state is PlayerExecutionState and the Player is reached
// only through the per-call PlayerExecutionPort.
#include "player/execution/player_execution.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <bq_log/bq_log.h>

#include "football/animation/library.hpp"
#include "football/ball/ball.hpp"
#include "football/player/execution/player_motion.hpp"
#include "player/execution/player_action_arbitration.hpp"
#include "player/execution/player_action_motion.hpp"
#include "player/execution/player_control_builder.hpp"
#include "player/execution/player_motion_constants.hpp"
#include "player/execution/player_runtime_sink.hpp"
#include "player/execution/player_touch_math.hpp"
#include "player/player.hpp"

using namespace football::math;
using football::ball::Ball;
using football::sim::PlayerRuntimeSink;
using football::sim::player::PlayerObservation;
using football::sim::player::PlayerTouchSink;

constexpr bool useContinuousBallCheck = true;
constexpr int initialReQueueDelayFrames = 22;
constexpr bool allowReQueue = true;
constexpr bool allowPreTouchRotationSmuggle = false;


// ---------------------------------------------------------------------------
// The stack-local frame for one action tick. It carries the values the phases
// already used as locals; none of it is persistent executor state.
// ---------------------------------------------------------------------------
struct PlayerExecution::ActionTickFrame {
  football::sim::Tick now{};
  const football::sim::PlayerTickContext* tick = nullptr;
  const PlayerObservation* observation = nullptr;
  PlayerTouchSink* touch_sink = nullptr;
  PlayerRuntimeSink* runtime_sink = nullptr;
  Player* ball_retainer = nullptr;
  const PlayerActionState* action = nullptr;
  PlayerKinematicState tick_start_state;
  std::chrono::milliseconds mental_image_age{};
  PlayerCommandQueue request_command_queue;
  bool has_request_input = false;
  bool reaction_executed = false;
  bool movement_published_this_tick = false;
  bool continuity_repair_due = false;
  bool legacy_opportunity = false;
  bool simulation_due = false;
};

PlayerExecution::PlayerExecution(const AnimationLibrary& animations)
    : animations_(animations) {}

PlayerExecution::~PlayerExecution() = default;

// ---------------------------------------------------------------------------
// Lifecycle. The Player owns activation/deactivation as gameplay; the reset
// sequence, the special action install and the compatibility re-read belong to
// the process and stay here.
// ---------------------------------------------------------------------------
void PlayerExecution::Activate(PlayerExecutionPort& owner, const Vector3& spawn_position) {
  PortScope scope(*this, owner);
  // A fresh actor must start from fresh compatibility storage, exactly as a
  // newly-constructed Humanoid did before the storage became Player-owned.
  state_.legacy_anim_state_ = Anim{};
  state_.legacy_spatial_state_ = SpatialState{};
  // The old Humanoid constructor performed one zero-position reset before the
  // explicit formation-position reset. Keep both, in order, so the resulting
  // idle entry frame is unchanged.
  ResetLegacyPosition(Vector3(0), Vector3(0));
  ResetLegacyPosition(spawn_position, Vector3(0));
  state_.legacy_state_initialized_ = true;
  port().CommitLegacyActionMotion();
  // ANIM-1 A6.3c.1: a reset is a special action install, not an out-of-band
  // cursor write. Route it through the same install entry so `installed_action_`
  // stays valid and the deterministic entry frame is carried in the selection.
  InstallSelectedAction(CurrentSelectedAction());
}

void PlayerExecution::Deactivate(PlayerExecutionPort& owner, football::sim::Tick now) {
  PortScope scope(*this, owner);
  // Preserve both historical resets, including their epochs.
  ResetSituation(owner, port().GetPosition(), now);
  ResetSituation(owner, port().GetPosition(), now);
}

void PlayerExecution::Exit(PlayerExecutionPort& owner, football::sim::Tick now) {
  PortScope scope(*this, owner);
  // Preserve the former base destructor's reset window without invoking roster
  // callbacks: Team::Exit may already have deleted other players.
  ResetRuntimeState(port().GetPosition(), now);
}

void PlayerExecution::ResetPosition(PlayerExecutionPort& owner, const Vector3& new_position,
                                    const Vector3& focus_position) {
  PortScope scope(*this, owner);
  ResetLegacyPosition(new_position, focus_position);
  // ANIM-1.1: re-placement clears any pending active/passive request.
  state_.pending_action_request_.reset();
  state_.playback_state_.Reset();
  state_.pending_reaction_.reset();
  state_.pending_passive_trip_.reset();
  port().CommitLegacyActionMotion();
  // ANIM-1 A6.3c.1: a reset is a special action install.
  InstallSelectedAction(CurrentSelectedAction());
}

void PlayerExecution::ResetSituation(PlayerExecutionPort& owner,
                                     const Vector3& focus_position,
                                     football::sim::Tick now) {
  PortScope scope(*this, owner);
  ResetRuntimeState(focus_position, now);
}

void PlayerExecution::ResetRuntimeState(const Vector3& focus_position,
                                        football::sim::Tick now) {
  port().ResetTouchRecord();
  // ANIM-1.1: a reset must not leave a stale active/passive request behind.
  state_.pending_action_request_.reset();
  state_.playback_state_.Reset();
  state_.pending_reaction_.reset();
  state_.pending_passive_trip_.reset();
  if (port().IsActive()) {
    // A reset is also a continuity break, so any earlier intent is invalid.
    ++state_.decisionLocomotionState.continuityEpoch;
    ResetLegacySituation(focus_position);
    port().CommitLegacyActionMotion();
    // ANIM-1 A6.3c.1: the runtime reset is the same special action install.
    InstallSelectedAction(CurrentSelectedAction());
  }
}

// ---------------------------------------------------------------------------
// Control input. Continuous movement intent, never a discrete action.
// ---------------------------------------------------------------------------
void PlayerExecution::SetControl(PlayerExecutionPort& owner, const PlayerControl& control) {
  PortScope scope(*this, owner);
  state_.control_ = control;
}

void PlayerExecution::ClearControl(PlayerExecutionPort& owner) {
  PortScope scope(*this, owner);
  state_.control_.reset();
}

float PlayerExecution::ControlSpeed() const {
  if (state_.control_) return state_.control_->desired_speed;
  return 0.f;
}

bool PlayerExecution::AllowLastDitch(
    const football::player::PlayerBallRelation& ball_relation,
    float team_possession_amount, bool include_possession_amount) const {
  if (include_possession_amount && team_possession_amount < 1.0f) return true;
  return ball_relation.optimistic_ms * 1.7f + 800 < ball_relation.time_to_ball_ms;
}

void PlayerExecution::SelectRetainAnim(PlayerExecutionPort& owner) {
  PortScope scope(*this, owner);
  SelectRetainAnim();
}

bool PlayerExecution::IsEligibleForProceduralLocomotion(bool retaining_ball) const {
  return state_.actionState.IsPureLocomotion(retaining_ball);
}

int PlayerExecution::FrameCount() const {
  return static_cast<int>(GetCurrentBakedClip().frame_count);
}

PoseFrame PlayerExecution::Pose(PlayerExecutionPort& owner) const {
  PortScope scope(*this, owner);
  if (state_.playback_state_.have_pose) return state_.playback_state_.final_pose;
  return SampleBakedPose(port().GetKinematicState(), port().BodyProfile()).pose;
}

PlayerBakedPoseSample PlayerExecution::BakedPoseSample(PlayerExecutionPort& owner) const {
  PortScope scope(*this, owner);
  PlayerBakedPoseSample sample =
      SampleBakedPose(port().GetKinematicState(), port().BodyProfile());
  // ANIM-1 A5: the execution owns the final pose; SampleBakedPose supplies the
  // clip/frame metadata.
  if (state_.playback_state_.have_pose) {
    sample.pose = state_.playback_state_.final_pose;
    sample.base_angle = state_.playback_state_.final_base_angle;
  }
  return sample;
}


PlayerExecution::Projection PlayerExecution::CompatibilityProjection() const {
  return Projection{state_.legacy_spatial_state_.position,
                    state_.legacy_spatial_state_.movement,
                    state_.legacy_spatial_state_.bodyDirectionVec};
}


// ---------------------------------------------------------------------------
// ANIM-1 A9.7: the per-tick action orchestration, relocated verbatim from
// Humanoid::Process. The touch publication half lives in
// player_touch_execution.cpp; this method owns the decision, action-clock and
// request sequence around it.
// ---------------------------------------------------------------------------
void PlayerExecution::RunActionTick(PlayerExecutionPort& owner, football::sim::Tick now,
                           const football::sim::PlayerTickContext& tick,
                           const football::sim::player::PlayerObservation& observation,
                           football::sim::player::PlayerTouchSink& touch_sink,
                           football::sim::PlayerRuntimeSink& runtime_sink) {
  PortScope scope(*this, owner);
  if (state_.startPos.coords[2] != 0.f) {
    throw std::logic_error(
        "PlayerExecution::RunActionTick: player position must have zero height");
  }

  ActionTickFrame frame;
  frame.now = now;
  frame.tick = &tick;
  frame.observation = &observation;
  frame.touch_sink = &touch_sink;
  frame.runtime_sink = &runtime_sink;
  frame.ball_retainer = tick.ball_retainer;

  ConsumeActionInputs(frame);
  BeginActionTick(frame);
  RefreshDecisionQueue(frame);
  AdvanceActionAndMovement(frame);
  EvaluateActionRequeue(frame);
  SelectActionForFrame(frame);
  PublishCadenceLocomotion(frame);
  ApplyPostActionTick(frame);
}





// ---------------------------------------------------------------------------
// P3: RunActionTick phases. Each body is the original block moved verbatim;
// only the former function locals now live in the stack-local ActionTickFrame.
// The call order is unchanged.
// ---------------------------------------------------------------------------
void PlayerExecution::ConsumeActionInputs(ActionTickFrame& frame) {
  // ANIM-2 A11.5: the single pending-input boundary, consumed once per real
  // action tick. A passive reaction is the physics/action hand-off: its physical
  // fact is already applied, only the animation reaction waits here. An accepted
  // active request is translated into the same command queue the controller path
  // uses, so both funnel through one selection/install/execution pipeline.
  if (const auto pending_reaction = ConsumePendingReaction()) {
    if (pending_reaction->kind == ReactionKind::Trip) {
      if (const auto pending_trip = ConsumePendingPassiveTrip()) {
        state_.interruptAnim = e_InterruptAnim_Trip;
        state_.tripDirection = pending_trip->direction;
        state_.tripType = pending_trip->trip_type;
        frame.reaction_executed = true;
        ++audit_.reaction_trip_interrupts_scheduled_;
        // ANIM-2 A11.7: a Trip preempts an accepted active intent. Discard it
        // so a stale Play request cannot switch the actor back out of the trip.
        if (state_.pending_action_request_) {
          state_.pending_action_request_.reset();
          ++audit_.action_request_preemptions_;
        }
      }
    }
  }
  if (!frame.reaction_executed) {
    if (const auto pending_request = ConsumePendingActionRequest()) {
      // ANIM-2 A11.7: re-arbitrate at consumption. The request was accepted on
      // an earlier tick and the action state may have changed since, so a now-
      // illegal intent is dropped instead of force-switching the running action.
      if (ArbitrateActionRequest(state_.actionState, pending_request->action) ==
          RequestOutcome::Accepted) {
        const CommandBuildResult built =
            BuildCommandQueueForRequest(*pending_request, *frame.tick);
        if (built.status ==
            football::sim::player::CommandBuildStatus::Accepted) {
          frame.request_command_queue = std::move(built.commands);
          if (!frame.request_command_queue.empty()) {
            frame.has_request_input = true;
            // An explicit input is not a requeue opportunity: force the switch.
            state_.interruptAnim = e_InterruptAnim_Switch;
            ++audit_.action_request_inputs_consumed_;
          }
        } else {
          // Arbitration accepted the request, but the concrete Command is not
          // executable. Consume it, report the build status, and keep the
          // current action/movement instead of pretending the action ran.
          ++audit_.action_request_build_failures_;
          state_.action_feedback_.source_action = pending_request->action;
          state_.action_feedback_.command_status = built.status;
          state_.action_feedback_.type = e_FunctionType_None;
          state_.action_feedback_.installed = false;
          state_.action_feedback_.contact_due = false;
          state_.action_feedback_.touch_prepared = false;
          state_.action_feedback_.touch_accepted = false;
          state_.action_feedback_.completed = false;
          state_.action_feedback_.interrupted = false;
          state_.action_feedback_.start_frame = -1;
          state_.action_feedback_.touch_frame = -1;
          state_.action_feedback_.prepared_touch_tick = {};
        }
      }
    }
  }
}

void PlayerExecution::BeginActionTick(ActionTickFrame& frame) {
  // A prepared touch is only proof of acceptance after Runtime has committed
  // it. The touch record advances and names this player at the next tick.
  if (state_.action_feedback_.touch_prepared && !state_.action_feedback_.touch_accepted &&
      port().GetLastTouchTick() >= state_.action_feedback_.prepared_touch_tick &&
      frame.tick->touches.last_player_by_team[port().GetTeamID()] == port().GetID()) {
    state_.action_feedback_.touch_accepted = true;
  }
  // The sample age is frozen here; the line below advances `state_.mentalImageTime`
  // for the next tick, so later lookups must reuse this captured value.
  frame.mental_image_age = state_.mentalImageTime;
  // this might be the solution to long-term imbalance
  state_.decayingPositionOffset *= 0.95f;
  if (state_.decayingPositionOffset.GetLength() < 0.005) state_.decayingPositionOffset.Set(0);
  state_.decayingDifficultyFactor = clamp(state_.decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);

  frame.ball_retainer = frame.tick->ball_retainer;

  bool instaDoorheb = false;
  if (frame.tick->touches.last_team == port().GetTeamID()) instaDoorheb = true;
  state_.mentalImageTime = std::chrono::milliseconds{
      instaDoorheb ? 0 : port().GetReactionTime_ms()};

  // The authoritative movement state at tick start. Captured before
  // CalculateSpatialState so the procedural model integrates from the world
  // state in force, not from the animation prediction for this tick.
  frame.tick_start_state = port().GetKinematicState();
  // Gameplay continuity advances once per real Player tick. The epoch is
  // decided by eligibility, never by telemetry bookkeeping.
  AdvanceLocomotionContinuity(
      IsEligibleForProceduralLocomotion(
          frame.ball_retainer == self()));
}

void PlayerExecution::RefreshDecisionQueue(ActionTickFrame& frame) {
  // 4f-a3a: the complete decision queue has its own simulation clock. Its
  // cadence depends on world context, never on animation requeue opportunities.
  const football::sim::Tick decision_now = frame.now;
  const float decision_distance_to_ball =
      (frame.tick->ball.state().position.Get2D() -
       frame.tick_start_state.position)
          .GetLength();
  const bool designated_possession_player =
      frame.tick->designated_possession_player == self();
  const bool designated_team_possession_player =
      frame.tick->own_team.DesignatedPlayerId() == port().GetID();
  const auto player_decision_cadence = PlayerDecisionCadenceForContext(
      designated_possession_player, designated_team_possession_player,
      decision_distance_to_ball);
  const bool continuity_repair_due =
      IsEligibleForProceduralLocomotion(
          frame.ball_retainer == self()) &&
      DecisionLocomotionEpochIsStale();
  const bool player_decision_due =
      IsPlayerDecisionRefreshDue(
          decision_now, player_decision_cadence);
  frame.continuity_repair_due = continuity_repair_due;
  if (continuity_repair_due || player_decision_due) {
    const CommandBuildResult built = RequestCommand(
        {frame.tick->ball, frame.ball_retainer, frame.tick->pitch,
         frame.tick->own_team, frame.tick->authorization});
    PlayerCommandQueue player_decision_commands = std::move(built.commands);
    PublishPlayerDecisionQueue(player_decision_commands, decision_now);
  }

  // Continuity repair forces the independent Player Decision Clock, then
  // publishes its fresh Movement axis before locomotion execution. It does not
  // ask the controller through the legacy locomotion/animation queue.
  if (continuity_repair_due) {
    if (HasPlayerDecisionQueue() &&
        PublishMovementIntentFromQueue(GetPlayerDecisionQueue())) {
      frame.movement_published_this_tick = true;
      CommitLocomotionIntentRefresh(
          frame.tick->ball, decision_now, frame.ball_retainer == self());
    }
  }
}

void PlayerExecution::AdvanceActionAndMovement(ActionTickFrame& frame) {
  CalculateSpatialState(frame.ball_retainer);
  state_.legacy_spatial_state_.positionOffsetMovement = Vector3(0);
  // H3e1c-3c: pure locomotion ticks are produced by the simulation; every
  // other tick keeps the legacy root motion. Either way the legacy movement
  // fields end up as a projection of the authoritative kinematic state.
  ProjectMovementState(frame.tick_start_state, frame.ball_retainer,
                       frame.tick->restart_needs_simulation);

  AdvanceActionTick();

  frame.action = &ActionState();

  if (frame.action->IsAtLastFrame() && state_.interruptAnim == e_InterruptAnim_None) {
    state_.interruptAnim = e_InterruptAnim_Switch;
  }
}

void PlayerExecution::EvaluateActionRequeue(ActionTickFrame& frame) {
  bool mayReQueue = allowReQueue && !frame.has_request_input;

  // already some anim interrupt waiting?
  if (mayReQueue) {
    if (state_.interruptAnim != e_InterruptAnim_None) {
      mayReQueue = false;
    }
  }

  // may requeue on this frame?
  if (mayReQueue) {
    bool frameNumPredicate = false;
    float actionDistance =
        ((state_.legacy_spatial_state_.position + state_.legacy_spatial_state_.movement * 0.1f) -
         frame.tick->ball.Predict(football::sim::TickSpan{10})
             .position.Get2D())
            .GetLength();

    int team_id = frame.tick->processing_slot;
    using namespace football::sim::player_timing;
    const auto phase = static_cast<std::uint64_t>(team_id);
    if (frame.tick->designated_possession_player == self() &&
        actionDistance < 3.0f) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kOwnerNear, football::sim::TickSpan{phase});
    } else if (frame.tick->designated_possession_player == self()) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kOwner, football::sim::TickSpan{phase});
    } else if (frame.tick->own_team.DesignatedPlayerId() == port().GetID()) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kTeamOwner, football::sim::TickSpan{phase * 2});
    } else if (actionDistance < 5.0f) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kNearBall, football::sim::TickSpan{phase * 2});
    } else if (actionDistance < 10.0f) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kApproachingBall, football::sim::TickSpan{phase * 4});
    }

    if (!frameNumPredicate) mayReQueue = false;
  }

  // right anim to requeue?
  if (mayReQueue) {
    float ballDistance =
        (frame.observation->BallPrediction(
             frame.mental_image_age, 500, frame.now, frame.tick->ball)
             .Get2D() -
         state_.legacy_spatial_state_.position)
            .GetLength();
    if (((frame.action->type == e_FunctionType_Movement &&
          !frame.tick->ball_relation.locally_controllable &&
              ballDistance < 16.0f) ||
         (frame.action->type == e_FunctionType_Movement &&
          frame.tick->ball_relation.locally_controllable) ||
         (frame.action->type == e_FunctionType_Trap && ActionState().IsContactPending()) ||
         (frame.action->type == e_FunctionType_BallControl &&
          ActionState().IsContactPending())) &&
        GetCurrentBakedClip().metadata.incoming_special_state.empty() &&
        GetCurrentBakedClip().metadata.outgoing_special_state.empty()) {
      mayReQueue = true;
    } else {
      mayReQueue = false;
    }
  }

  if (mayReQueue) {
    state_.interruptAnim = e_InterruptAnim_ReQueue;
  }

  frame.legacy_opportunity = state_.interruptAnim != e_InterruptAnim_None;
  frame.simulation_due =
      NoteLocomotionIntentCadence(frame.now, frame.ball_retainer == self());
}

void PlayerExecution::SelectActionForFrame(ActionTickFrame& frame) {
  if (!frame.legacy_opportunity) return;

  PlayerCommandQueue commandQueue;      // selection queue

  const bool trip_local_queue =
      state_.interruptAnim == e_InterruptAnim_Trip && state_.tripType != 0;
  if (frame.has_request_input) {
    commandQueue = std::move(frame.request_command_queue);
  } else if (trip_local_queue) {
    // ANIM-1 A7 step 2: the Player owns the local trip command construction.
    BuildTripSelectionCommands(
        commandQueue, state_.tripDirection, state_.tripType,
        state_.legacy_spatial_state_.floatVelocity,
        state_.legacy_spatial_state_.directionVec,
        state_.legacy_spatial_state_.position);
    state_.tripType = 0;
  } else {
    if (!HasPlayerDecisionQueue()) {
      throw std::logic_error(
          "Player Decision queue missing before animation selection");
    }
    commandQueue = GetPlayerDecisionQueue();
  }

  // iterate through the command queue and pick the first that is applicable
  bool found = false;
  bool preferPassAndShot = false; // pass/shot and such; in that case we want
                                  // trap/ballcontrol anims to be less prefered
  for (unsigned int i = 0; i < commandQueue.size(); i++) {
    const PlayerCommand &command = commandQueue[i];

    if (command.desiredFunctionType == e_FunctionType_ShortPass ||
        command.desiredFunctionType == e_FunctionType_LongPass ||
        command.desiredFunctionType == e_FunctionType_HighPass ||
        command.desiredFunctionType == e_FunctionType_Shot) {
      preferPassAndShot = true;
    }
    found = TrySelectAction(
        frame.now, *frame.tick, command, *frame.observation, state_.interruptAnim,
        preferPassAndShot, allowPreTouchRotationSmuggle,
        useContinuousBallCheck);
    if (found) break;
  }

  if (state_.interruptAnim == e_InterruptAnim_Switch && !found) {
    const auto logger = bq::log::get_log_by_name("football");
    if (logger.is_valid()) {
      logger.warning(
          "PlayerExecution::RunActionTick: no applicable animation; current type {}",
          GetCurrentBakedClip().metadata.action_type);
      for (const auto& command : commandQueue) {
        logger.warning(
            "desired type {}, velocity {}, direction ({}, {}, {})",
            command.desiredFunctionType, command.desiredVelocityFloat,
            command.desiredDirection.coords[0],
            command.desiredDirection.coords[1],
            command.desiredDirection.coords[2]);
      }
      logger.warning(
          "current velocity {}, body angle {}, relative angle {}, state {}",
          state_.legacy_spatial_state_.floatVelocity,
          static_cast<float>(state_.legacy_spatial_state_.bodyAngle),
          static_cast<float>(state_.legacy_spatial_state_.relBodyAngle),
          GetCurrentBakedClip().metadata.outgoing_special_state);
    }
    throw std::runtime_error(
        "PlayerExecution::RunActionTick: no applicable animation");
  }

  if (found) {
    state_.startPos = state_.legacy_spatial_state_.position;
    state_.startAngle = state_.legacy_spatial_state_.angle;

    CalculatePredictedSituation(state_.nextStartPos, state_.nextStartAngle);

    // decaying difficulty
    float animDiff = GetCurrentBakedClip().metadata.difficulty;
    if (animDiff > state_.decayingDifficultyFactor) state_.decayingDifficultyFactor = animDiff;

    // if we just requeued, for example, from movement to ballcontrol, there's
    // no reason we can not immediately requeue to another ballcontrol again
    // (next time). only apply the initial requeue delay on subsequent anims of
    // the same type (so we can have a fast ballcontrol -> ballcontrol requeue,
    // but after that, use the initial delay)
    if (state_.interruptAnim == e_InterruptAnim_ReQueue &&
        state_.previousAnim_functionType == frame.action->type) {
      state_.reQueueDelayFrames = initialReQueueDelayFrames;
    }
  }
}

void PlayerExecution::PublishCadenceLocomotion(ActionTickFrame& frame) {
  // Movement publication belongs to the locomotion cadence, not animation
  // opportunities. A simultaneous Trip-local selection does not change the
  // cadence publisher's source: it always samples the serialized decision queue.
  if (frame.simulation_due && !frame.movement_published_this_tick) {
    if (!HasPlayerDecisionQueue()) {
      throw std::logic_error(
          "Player Decision queue missing before locomotion publication");
    }
    if (PublishMovementIntentFromQueue(GetPlayerDecisionQueue())) {
      frame.movement_published_this_tick = true;
      CommitLocomotionIntentRefresh(
          frame.tick->ball, frame.now, frame.ball_retainer == self());
    }
  }
  state_.reQueueDelayFrames = std::max(state_.reQueueDelayFrames - 1, 0);

  state_.interruptAnim = e_InterruptAnim_None;
}

void PlayerExecution::ApplyPostActionTick(ActionTickFrame& frame) {
  frame.ball_retainer = ProcessActionTouches(
      frame.now, *frame.tick, *frame.observation, frame.ball_retainer,
      *frame.touch_sink, *frame.runtime_sink);
  // ANIM-1 A9 step 2: the action/movement/rotation smuggle evolution is a pure
  // PlayerMotion calculation; this applies its deltas in the legacy order.
  ActionSmuggleTickInput smuggle_input;
  smuggle_input.frame_num = state_.legacy_anim_state_.frameNum;
  // ANIM-2 A18.4: the action's entry frame, so the smuggle is spread over the
  // frames that actually remain.
  smuggle_input.start_frame = state_.installed_action_.start_frame;
  smuggle_input.touch_frame = state_.legacy_anim_state_.touchFrame;
  smuggle_input.frame_count = static_cast<int>(GetCurrentBakedClip().frame_count);
  smuggle_input.action_smuggle = state_.legacy_anim_state_.actionSmuggle;
  smuggle_input.movement_smuggle = state_.legacy_anim_state_.movementSmuggle;
  smuggle_input.rotation_smuggle_begin = state_.legacy_anim_state_.rotationSmuggle.begin;
  smuggle_input.rotation_smuggle_end = state_.legacy_anim_state_.rotationSmuggle.end;
  smuggle_input.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
  const ActionSmuggleTickResult smuggle =
      PlayerActionMotion::ActionSmuggleTick(smuggle_input);

  state_.legacy_spatial_state_.actionSmuggleMovement = smuggle.action_smuggle_movement;
  state_.legacy_anim_state_.actionSmuggleOffset += smuggle.action_smuggle_offset_delta;
  state_.legacy_spatial_state_.movementSmuggleMovement = smuggle.movement_smuggle_movement;
  state_.legacy_anim_state_.movementSmuggleOffset += smuggle.movement_smuggle_offset_delta;
  state_.legacy_anim_state_.rotationSmuggleOffset = smuggle.rotation_smuggle_offset;

  // ANIM-1 A9 step 3: the goalkeeper boundary clamps are Player-owned.
  ApplyGoalkeeperBoundaryClamps(*frame.tick, frame.ball_retainer);

  // ANIM-1 A5: the Player owns the final pose. The action tick only hands over
  // the selected clip/frame and the base yaw once per tick; the Player's
  // playback state samples/blends it.
  // ANIM-1 A6.3c.3: the clip id and frame come from the Player authority.
  UpdatePlaybackFromAuthority(
      frame.now, state_.startAngle + state_.legacy_anim_state_.rotationSmuggleOffset,
      football::animation::BlendTicksForSwitch(
          state_.legacy_anim_state_.functionType == e_FunctionType_Movement &&
          IsEligibleForProceduralLocomotion(
              frame.ball_retainer == self())));
}

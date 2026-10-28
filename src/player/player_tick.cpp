// ANIM-1 A9.5-A9.8: the Player-owned action tick, state projection and the
// legacy selection support that used to live in Humanoid/HumanoidBase.
//
// The bodies here are verbatim moves from humanoidbase.cpp (and the
// non-touch part of Humanoid::Process); only the receiver changed, so the
// operation order, floating-point expressions are unchanged.
#include "player/player.hpp"

#include <bq_log/bq_log.h>

#include "football/ball/ball.hpp"
#include "player/execution/player_control_builder.hpp"

#include <algorithm>
#include <functional>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"
#include "football/animation/selection_math.hpp"
#include "football/player/command/player_motion.hpp"
#include "player/execution/player_action_motion.hpp"
#include "player/execution/player_touch_math.hpp"
#include "player/command/player_motion_constants.hpp"
#include "player/execution/player_runtime_sink.hpp"

using namespace football::math;
using football::ball::Ball;
using std::placeholders::_1;
using std::placeholders::_2;

constexpr bool useContinuousBallCheck = true;
constexpr int initialReQueueDelayFrames = 22;
constexpr bool allowReQueue = true;
constexpr bool allowPreTouchRotationSmuggle = false;

// 4b''-player-noquery-audit: provenance stays local to the selection queue.
// It is deliberately not part of PlayerCommand or serialized simulation state.
enum class PlayerPathSelectionCommandProvenance {
  Controller,
  LocalTrip,
  LocalTripMovementFallback
};








void Player::ResetLegacyPosition(const Vector3 &newPos,
                                 const Vector3 &focusPos) {

  startPos = newPos;
  startAngle = FixAngle((focusPos - newPos).GetNormalized(Vector3(0, -1, 0)).GetAngle2D());
  nextStartPos = startPos;
  nextStartAngle = startAngle;
  previousPosition2D = startPos;

  legacy_spatial_state_.position = startPos;
  legacy_spatial_state_.angle = startAngle;
  legacy_spatial_state_.directionVec = Vector3(0, -1, 0).GetRotated2D(startAngle);
  legacy_spatial_state_.floatVelocity = 0;

  legacy_spatial_state_.movement = Vector(0);
  legacy_spatial_state_.actionSmuggleMovement = Vector(0);
  legacy_spatial_state_.movementSmuggleMovement = Vector(0);
  legacy_spatial_state_.positionOffsetMovement = Vector(0);
  legacy_spatial_state_.relBodyAngleNonquantized = 0;

  legacy_spatial_state_.enumVelocity = e_Velocity_Idle;
  legacy_spatial_state_.floatVelocity = 0;
  legacy_spatial_state_.movement = Vector3(0);
  legacy_spatial_state_.relBodyDirectionVec = Vector3(0, -1, 0);
  legacy_spatial_state_.relBodyAngle = 0;
  // After a reset the body faces the focus direction, matching the locomotion
  // direction and the animation base angle (startAngle). Otherwise the derived
  // GetFacing() (velocity=0 -> bodyFacing) would report world -Y instead of the
  // direction the reset actually asked for. relBodyDirectionVec/relBodyAngle
  // stay (0,-1,0)/0 because body and locomotion now agree.
  legacy_spatial_state_.bodyDirectionVec = legacy_spatial_state_.directionVec;
  legacy_spatial_state_.bodyAngle = legacy_spatial_state_.angle;
  legacy_spatial_state_.foot = e_Foot_Right;

  const AnimationId idleAnimID = SelectIdleMovementAnim(
      animations_, legacy_spatial_state_.position, legacy_spatial_state_.angle,
      legacy_spatial_state_.floatVelocity);
  legacy_anim_state_.animationId = idleAnimID;
  legacy_anim_state_.positions.clear();
  legacy_anim_state_.positions = GetBakedClip(legacy_anim_state_.animationId).root_positions;
  // Deterministic entry frame: a fresh reset always starts the idle clip at
  // its first legal frame instead of drawing a random start.
  legacy_anim_state_.frameNum = 0;
  legacy_anim_state_.touchFrame = -1;
  legacy_anim_state_.actionSmuggle = Vector3(0);
  legacy_anim_state_.actionSmuggleOffset = Vector3(0);
  legacy_anim_state_.actionSmuggleSustain = Vector3(0);
  legacy_anim_state_.actionSmuggleSustainOffset = Vector3(0);
  legacy_anim_state_.movementSmuggle = Vector3(0);
  legacy_anim_state_.movementSmuggleOffset = Vector3(0);
  legacy_anim_state_.rotationSmuggle.begin = 0;
  legacy_anim_state_.rotationSmuggle.end = 0;
  legacy_anim_state_.rotationSmuggleOffset = 0;
  legacy_anim_state_.functionType = e_FunctionType_Movement;
  legacy_anim_state_.incomingMovement = Vector3(0);
  legacy_anim_state_.outgoingMovement = Vector3(0);
  legacy_anim_state_.positionOffset = Vector3(0);

  previousAnim_functionType = e_FunctionType_Movement;



  interruptAnim = e_InterruptAnim_None;
  tripType = 0;

  decayingPositionOffset = Vector3(0);
  decayingDifficultyFactor = 0.0f;

  reQueueDelayFrames = 0;
  tripDirection = Vector3(0);
}

void Player::ApplyLegacyPositionOffset(const Vector3 &offset) {

  assert(offset.coords[2] == 0.0f);

  nextStartPos += offset;
  startPos += offset;
  legacy_spatial_state_.position += offset;
  legacy_spatial_state_.positionOffsetMovement += offset * 100.0f;
  decayingPositionOffset += offset;
  if (decayingPositionOffset.GetLength() > 0.1f) decayingPositionOffset = decayingPositionOffset.GetNormalized() * 0.1f;
  legacy_anim_state_.positionOffset += offset;
}



void Player::ResetLegacySituation(const Vector3 &focusPos) {
  mentalImageTime = std::chrono::milliseconds{0};
  ResetLegacyPosition(legacy_spatial_state_.position, focusPos);
}

bool Player::_HighOrBouncyBall(const Ball& ball) const {
  float ballHeight1 = ball.Predict(football::sim::TickSpan{1}).position.coords[2];
  float ballHeight2 = ball.Predict(football::sim::TickSpan{defaultTouchOffset_ms / football::sim::kMillisecondsPerTick}).position.coords[2];
  float ballBounce = std::fabs(ball.state().velocity.coords[2]);
  bool highBall = false;
  if (ballHeight1 > 0.3f || ballHeight2 > 0.3f) {
    highBall = true;
  } else if (ballBounce > 1.0f) {
      // low balls are also treated as 'high ball' when there's a
                    // lot of bounce going on (hard to control)
    highBall = true;
  }
  return highBall;
}

// ALERT: set sorting predicates before calling this function

// ALERT: set sorting predicates before calling this function


void Player::CalculatePredictedSituation(Vector3 &predictedPos,
                                               radian &predictedAngle) {

  if (legacy_anim_state_.positions.size() > (unsigned int)legacy_anim_state_.frameNum) {
    assert(legacy_anim_state_.positions.size() > (unsigned int)(static_cast<int>(GetCurrentBakedClip().frame_count) - 1));
    predictedPos = legacy_spatial_state_.position + legacy_anim_state_.positions.at((static_cast<int>(GetCurrentBakedClip().frame_count) - 1)) + legacy_anim_state_.actionSmuggle + legacy_anim_state_.actionSmuggleSustain + legacy_anim_state_.movementSmuggle;
  } else {
    predictedPos = legacy_spatial_state_.position + GetCurrentBakedClip().metadata.translation.Get2D().GetRotated2D(legacy_spatial_state_.angle) + legacy_anim_state_.actionSmuggle + legacy_anim_state_.actionSmuggleSustain + legacy_anim_state_.movementSmuggle;
  }

  predictedAngle = legacy_spatial_state_.angle + GetCurrentBakedClip().metadata.outgoing_angle + legacy_anim_state_.rotationSmuggle.end;
  predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);
  assert(predictedPos.coords[2] == 0.0f);
}

Vector3 Player::CalculateOutgoingMovement(const std::vector<Vector3> &positions) const {
  if (positions.size() < 2) return 0;
  return (positions.at(positions.size() - 1) - positions.at(positions.size() - 2)) * 100.0f;
}

bool Player::UsesProceduralLocomotion(const Player* ball_retainer) const {
  // Execution authority is the Player Decision Clock's current-epoch intent, not
  // the animation command. Executability is the gate; the producer contract is a
  // separate fatal oracle.
  return IsEligibleForProceduralLocomotion(ball_retainer == this) &&
         HasExecutableDecisionLocomotionIntent();
}


void Player::CalculateSpatialState(const Player* ball_retainer) {
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d2: root-motion and body-pose producers are dead on this path. Foot
    // remains animation-owned gait/selection state until its separate audit.
    if (legacy_anim_state_.frameNum > 12) {
      legacy_spatial_state_.foot = static_cast<e_Foot>(GetCurrentBakedClip().metadata.outgoing_foot);
    }
    ++HumanoidProceduralMovementTicks();
    return;
  }
  // ANIM-1 A8 step 4: the non-procedural (baked action) motion is computed and
  // committed by the Player. This only keeps the compatibility projection that
  // the Player writes for later same-tick readers.
  ComputeAndCommitActionMotion();
}


void Player::ProjectSimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  legacy_spatial_state_.position = state.position;
  legacy_spatial_state_.movement = state.velocity;
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d1: these serialized legacy aliases have no pure-locomotion producer.
    // Project the authoritative velocity rather than retaining animation history.
  }
  legacy_spatial_state_.directionVec = state.GetFacing();
  legacy_spatial_state_.floatVelocity = legacy_spatial_state_.movement.GetLength();
  legacy_spatial_state_.enumVelocity = FloatToEnumVelocity(legacy_spatial_state_.floatVelocity);
  legacy_spatial_state_.angle =
      ModulateIntoRange(-pi, pi, FixAngle(legacy_spatial_state_.directionVec.GetAngle2D()));
  ApplySimulationBodyState(state);
  // The next tick derives its raw movement from this, so it must describe the
  // position actually in force. Collision corrections land here too, exactly
  // as they did when the legacy root motion owned the position.
  previousPosition2D = legacy_spatial_state_.position;
}

void Player::ApplySimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  // Legacy action path: animation root motion produced this state, so adopting
  // it as the Player's motion is correct here -- and is the only remaining
  // source-explicit entry that re-reads the compatibility projection.
  ProjectSimulationMovementState(state, ball_retainer);
  CommitLegacyActionMotion();
}

void Player::ApplySimulationBodyState(
    const PlayerKinematicState &state) {
  // Continuous simulation truth. Nothing in this function writes back into
  // state.bodyFacing, so legacy quantization cannot corrupt torso authority.
  legacy_spatial_state_.bodyDirectionVec = state.bodyFacing;
  legacy_spatial_state_.bodyAngle =
      legacy_spatial_state_.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  const Vector3 relative = state.bodyFacing.GetRotated2D(-legacy_spatial_state_.angle)
                               .GetNormalized(Vector3(0, -1, 0));
  legacy_spatial_state_.relBodyAngleNonquantized =
      relative.GetAngle2D(Vector3(0, -1, 0));

  // Compatibility for animation selection only. This quantization is a leaf:
  // bodyDirectionVec remains the continuous simulation projection above.
  legacy_spatial_state_.relBodyDirectionVec =
      ForceIntoAllowedBodyDirectionVec(relative);
  legacy_spatial_state_.relBodyAngle =
      legacy_spatial_state_.relBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
}


void Player::ProjectMovementState(
    const PlayerKinematicState &tickStartState, const Player* ball_retainer,
    bool restart_needs_simulation) {

  if (!UsesProceduralLocomotion(ball_retainer)) {
    // Non-locomotion ticks keep the legacy animation root motion. Projecting
    // it keeps the compatibility movement fields a single projection of the
    // simulation kinematic state.
    ApplySimulationMovementState(GetKinematicState(), ball_retainer);
    return;
  }

  // Pure locomotion: the simulation produces the movement and the legacy
  // compatibility movement fields follow it. The command and the starting state are
  // the tick-start ones, so an action selection later in this tick cannot
  // change what this tick's locomotion was. Writing legacy_spatial_state_.position here
  // means any later action already starts from the authoritative position
  // instead of an animation one.
  // The adapter consumes only the tick-start simulation state. Locomotion
  // produces lower-body motion first; the independent torso model then uses the
  // resulting facing and the command's look target.
  // The tick that consumes the command must see an exact copy of it.
  // Pure locomotion: one command source, the Player Decision Clock. Both the
  // movement model and the torso model consume the same decision-owned intent,
  // so the animation command no longer decides what executes.
  CheckDecisionLocomotionIntentOracle();
  const PlayerCommand &command = GetDecisionLocomotionIntent();
  // P2a: Player computes the locomotion through PlayerMotion and commits the
  // resulting state. S0.1: this path only *projects* it into the legacy spatial
  // state -- it does not adopt it back, so the compatibility state can no longer overwrite
  // a locomotion tick the Player already owns. The oracle proves the two sides
  // agree at this point.
  const PlayerKinematicState next = CommitLocomotion(
      command, tickStartState, restart_needs_simulation, 0.01f);
  ProjectSimulationMovementState(next, ball_retainer);
  CheckSimulationKinematicOracle();
}













Vector3 Player::CalculatePhysicsVector(football::sim::Tick now, int animID, bool useDesiredMovement, const Vector3 &desiredMovement, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, radian &rotationOffset_ret) const {
  // ANIM-1 A6.4 A8.0: the candidate reference-motion kernel now lives in
  // kinematic/capability inputs and writes the plan back; the algorithm itself
  // is unchanged (see PlayerActionMotion::BuildActionMotionPlan).
  const AnimationClip &clip = GetBakedClip(animID);

  ActionMotionInput input;
  input.spatial_movement = legacy_spatial_state_.movement;
  input.angle = legacy_spatial_state_.angle;
  input.direction_vec = legacy_spatial_state_.directionVec;
  input.float_velocity = legacy_spatial_state_.floatVelocity;
  input.use_desired_movement = useDesiredMovement;
  input.desired_movement = desiredMovement;
  input.use_desired_body_direction = useDesiredBodyDirection;
  input.desired_body_direction_rel = desiredBodyDirectionRel;
  input.parameters.max_velocity = GetMaxVelocity();
  input.parameters.stat_agility = GetStat(football::player::PlayerStat::physical_agility);
  input.parameters.stat_acceleration = GetStat(football::player::PlayerStat::physical_acceleration);
  input.parameters.stat_velocity = GetStat(football::player::PlayerStat::physical_velocity);
  input.parameters.stat_dribble = GetStat(football::player::PlayerStat::technical_dribble);
  input.parameters.stat_balance = GetStat(football::player::PlayerStat::physical_balance);
  input.parameters.stat_technical_ballcontrol = GetStat(football::player::PlayerStat::technical_ballcontrol);
  input.parameters.last_touch_bias_1000 = GetLastTouchBias(1000, now);
  input.parameters.decaying_position_offset_length = decayingPositionOffset.GetLength();

  const ActionMotionPlan plan = PlayerActionMotion::BuildActionMotionPlan(clip, input);

  positions_ret = plan.positions;
  rotationOffset_ret = plan.rotation_offset;
  return plan.outgoing_velocity;
}


// ---------------------------------------------------------------------------
// ANIM-1 A9.7: the per-tick action orchestration, relocated verbatim from
// Humanoid::Process. The touch publication half lives in
// player_touch_execution.cpp; this method owns the decision, action-clock and
// request sequence around it.
// ---------------------------------------------------------------------------
void Player::RunActionTick(football::sim::Tick now,
                           const football::sim::PlayerTickContext& tick,
                           const football::sim::player::PlayerObservation& observation,
                           football::sim::player::PlayerTouchSink& touch_sink,
                           football::sim::PlayerRuntimeSink& runtime_sink) {
  if (startPos.coords[2] != 0.f) {
    throw std::logic_error(
        "Player::RunActionTick: player position must have zero height");
  }
  this->NoteProcessedPlayerTick();

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
void Player::ConsumeActionInputs(ActionTickFrame& frame) {
  // ANIM-2 A11.5: the single pending-input boundary, consumed once per real
  // action tick. A passive reaction is the physics/action hand-off: its physical
  // fact is already applied, only the animation reaction waits here. An accepted
  // active request is translated into the same command queue the controller path
  // uses, so both funnel through one selection/install/execution pipeline.
  if (const auto pending_reaction = this->ConsumePendingReaction()) {
    if (pending_reaction->kind == ReactionKind::Trip) {
      if (const auto pending_trip = this->ConsumePendingPassiveTrip()) {
        interruptAnim = e_InterruptAnim_Trip;
        tripDirection = pending_trip->direction;
        tripType = pending_trip->trip_type;
        frame.reaction_executed = true;
        ++reaction_trip_interrupts_scheduled_;
        // ANIM-2 A11.7: a Trip preempts an accepted active intent. Discard it
        // so a stale Play request cannot switch the actor back out of the trip.
        if (pending_action_request_) {
          pending_action_request_.reset();
          ++action_request_preemptions_;
        }
      }
    }
  }
  if (!frame.reaction_executed) {
    if (const auto pending_request = this->ConsumePendingActionRequest()) {
      // ANIM-2 A11.7: re-arbitrate at consumption. The request was accepted on
      // an earlier tick and the action state may have changed since, so a now-
      // illegal intent is dropped instead of force-switching the running action.
      if (ArbitrateActionRequest(actionState, pending_request->action) ==
          RequestOutcome::Accepted) {
        const CommandBuildResult built =
            this->BuildCommandQueueForRequest(*pending_request, *frame.tick);
        if (built.status ==
            football::sim::player::CommandBuildStatus::Accepted) {
          frame.request_command_queue = std::move(built.commands);
          if (!frame.request_command_queue.empty()) {
            frame.has_request_input = true;
            // An explicit input is not a requeue opportunity: force the switch.
            interruptAnim = e_InterruptAnim_Switch;
            ++action_request_inputs_consumed_;
          }
        } else {
          // Arbitration accepted the request, but the concrete Command is not
          // executable. Consume it, report the build status, and keep the
          // current action/movement instead of pretending the action ran.
          ++action_request_build_failures_;
          action_feedback_.source_action = pending_request->action;
          action_feedback_.command_status = built.status;
          action_feedback_.type = e_FunctionType_None;
          action_feedback_.installed = false;
          action_feedback_.contact_due = false;
          action_feedback_.touch_prepared = false;
          action_feedback_.touch_accepted = false;
          action_feedback_.completed = false;
          action_feedback_.interrupted = false;
          action_feedback_.start_frame = -1;
          action_feedback_.touch_frame = -1;
          action_feedback_.prepared_touch_tick = {};
        }
      }
    }
  }
}

void Player::BeginActionTick(ActionTickFrame& frame) {
  // A prepared touch is only proof of acceptance after Runtime has committed
  // it. The touch record advances and names this player at the next tick.
  if (action_feedback_.touch_prepared && !action_feedback_.touch_accepted &&
      GetLastTouchTick() >= action_feedback_.prepared_touch_tick &&
      frame.tick->touches.last_player_by_team[GetTeamID()] == GetID()) {
    action_feedback_.touch_accepted = true;
  }
  // The sample age is frozen here; the line below advances `mentalImageTime`
  // for the next tick, so later lookups must reuse this captured value.
  frame.mental_image_age = mentalImageTime;
  // this might be the solution to long-term imbalance
  decayingPositionOffset *= 0.95f;
  if (decayingPositionOffset.GetLength() < 0.005) decayingPositionOffset.Set(0);
  decayingDifficultyFactor = clamp(decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);

  frame.ball_retainer = frame.tick->ball_retainer;

  bool instaDoorheb = false;
  if (frame.tick->touches.last_team == this->GetTeamID()) instaDoorheb = true;
  mentalImageTime = std::chrono::milliseconds{
      instaDoorheb ? 0 : this->GetReactionTime_ms()};

  // The authoritative movement state at tick start. Captured before
  // CalculateSpatialState so the procedural model integrates from the world
  // state in force, not from the animation prediction for this tick.
  frame.tick_start_state = GetKinematicState();
  // gate-mismatch-context: exactly one attribution per real Player tick, before
  // any of this tick's CalculateSpatialState/ProjectMovementState evaluations.
  // Gameplay continuity advances before the audit observes this tick, so the
  // epoch is decided by eligibility rather than by telemetry bookkeeping.
  this->AdvanceLocomotionContinuity(
      this->IsEligibleForProceduralLocomotion(
          frame.ball_retainer == this));
  // re-entry audit must see every real player tick, eligible or not, because
  // leaving locomotion is what arms the next re-entry classification.
  this->NoteLocomotionReentryTick(
      this->IsEligibleForProceduralLocomotion(
          frame.ball_retainer == this),
      this->IsLocomotionIntentRefreshDue(frame.now), frame.now);
}

void Player::RefreshDecisionQueue(ActionTickFrame& frame) {
  // 4f-a3a: the complete decision queue has its own simulation clock. Its
  // cadence depends on world context, never on animation requeue opportunities.
  const football::sim::Tick decision_now = frame.now;
  const int decision_now_ms =
      static_cast<int>(football::sim::ToMilliseconds(decision_now));
  const float decision_distance_to_ball =
      (frame.tick->ball.state().position.Get2D() -
       frame.tick_start_state.position)
          .GetLength();
  const bool designated_possession_player =
      frame.tick->designated_possession_player == this;
  const bool designated_team_possession_player =
      frame.tick->own_team.DesignatedPlayerId() == GetID();
  const auto player_decision_cadence = PlayerDecisionCadenceForContext(
      designated_possession_player, designated_team_possession_player,
      decision_distance_to_ball);
  const bool continuity_repair_due =
      this->IsEligibleForProceduralLocomotion(
          frame.ball_retainer == this) &&
      this->DecisionLocomotionEpochIsStale();
  const bool player_decision_due =
      this->IsPlayerDecisionRefreshDue(
          decision_now, player_decision_cadence);
  frame.continuity_repair_due = continuity_repair_due;
  if (continuity_repair_due || player_decision_due) {
    const CommandBuildResult built = this->RequestCommand(
        {frame.tick->ball, frame.ball_retainer, frame.tick->pitch,
         frame.tick->own_team, frame.tick->authorization});
    PlayerCommandQueue player_decision_commands = std::move(built.commands);
    this->PublishPlayerDecisionQueue(player_decision_commands, decision_now);
    bool has_movement = false;
    for (const PlayerCommand &command : player_decision_commands)
      has_movement |= command.desiredFunctionType == e_FunctionType_Movement &&
                      command.useDesiredMovement;
    this->NoteControllerQuery(has_movement, decision_now,
                              frame.ball_retainer == this);
    RecordPlayerDecisionQuery(
        this, player_decision_commands, decision_now_ms,
        continuity_repair_due ? PlayerDecisionQueryCause::ContinuityRepair
                              : PlayerDecisionQueryCause::PlayerDecisionClockPeriodic,
        this->GetSimulationActionState().type);
    ++PlayerDecisionClockQueries();
    if (continuity_repair_due) ++PlayerDecisionClockForcedQueries();
    else ++PlayerDecisionClockPeriodicQueries();
  }

  // Continuity repair forces the independent Player Decision Clock, then
  // publishes its fresh Movement axis before locomotion execution. It does not
  // ask the controller through the legacy locomotion/animation queue.
  if (continuity_repair_due) {
    ++ContinuityRepairAttempts();
    this->NoteDecisionPublicationCause(2);
    if (this->HasPlayerDecisionQueue() &&
        this->PublishMovementIntentFromQueue(
            this->GetPlayerDecisionQueue(), decision_now,
            frame.ball_retainer == this)) {
      frame.movement_published_this_tick = true;
      this->CommitLocomotionIntentRefresh(
          frame.tick->ball, decision_now, frame.ball_retainer == this);
      ++ContinuityRepairPublications();
    } else {
      ++ContinuityRepairCandidatesMissing();
    }
    this->NoteDecisionPublicationCause(0);
  }
}

void Player::AdvanceActionAndMovement(ActionTickFrame& frame) {
  CalculateSpatialState(frame.ball_retainer);
  legacy_spatial_state_.positionOffsetMovement = Vector3(0);
  // H3e1c-3c: pure locomotion ticks are produced by the simulation; every
  // other tick keeps the legacy root motion. Either way the legacy movement
  // fields end up as a projection of the authoritative kinematic state.
  ProjectMovementState(frame.tick_start_state, frame.ball_retainer,
                       frame.tick->restart_needs_simulation);

  this->AdvanceActionTick();

  frame.action = &this->GetSimulationActionState();

  if (frame.action->IsAtLastFrame() && interruptAnim == e_InterruptAnim_None) {
    interruptAnim = e_InterruptAnim_Switch;
  }
}

void Player::EvaluateActionRequeue(ActionTickFrame& frame) {
  bool mayReQueue = allowReQueue && !frame.has_request_input;

  // already some anim interrupt waiting?
  if (mayReQueue) {
    if (interruptAnim != e_InterruptAnim_None) {
      mayReQueue = false;
    }
  }

  // may requeue on this frame?
  if (mayReQueue) {
    bool frameNumPredicate = false;
    float actionDistance =
        ((legacy_spatial_state_.position + legacy_spatial_state_.movement * 0.1f) -
         frame.tick->ball.Predict(football::sim::TickSpan{10})
             .position.Get2D())
            .GetLength();

    int team_id = frame.tick->processing_slot;
    using namespace football::sim::player_timing;
    const auto phase = static_cast<std::uint64_t>(team_id);
    if (frame.tick->designated_possession_player == this &&
        actionDistance < 3.0f) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kOwnerNear, football::sim::TickSpan{phase});
    } else if (frame.tick->designated_possession_player == this) {
      frameNumPredicate = StaggeredRefreshDue(
          frame.now, kOwner, football::sim::TickSpan{phase});
    } else if (frame.tick->own_team.DesignatedPlayerId() == GetID()) {
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
         legacy_spatial_state_.position)
            .GetLength();
    if (((frame.action->type == e_FunctionType_Movement &&
          !this->HasPossession() && ballDistance < 16.0f) ||
         (frame.action->type == e_FunctionType_Movement &&
          this->HasPossession()) ||
         (frame.action->type == e_FunctionType_Trap && TouchPending()) ||
         (frame.action->type == e_FunctionType_BallControl &&
          TouchPending())) &&
        GetCurrentBakedClip().metadata.incoming_special_state.empty() &&
        GetCurrentBakedClip().metadata.outgoing_special_state.empty()) {
      mayReQueue = true;
    } else {
      mayReQueue = false;
    }
  }

  if (mayReQueue) {
    interruptAnim = e_InterruptAnim_ReQueue;
  }

  frame.legacy_opportunity = interruptAnim != e_InterruptAnim_None;
  frame.simulation_due =
      this->NoteLocomotionIntentCadence(
          frame.legacy_opportunity, frame.now,
          frame.ball_retainer == this);
  // Tag the cause of any publication on this path, so a legacy-driven query is
  // not credited to the simulation cadence in the audit.
  frame.legacy_only = frame.legacy_opportunity && !frame.simulation_due;
  this->NoteDecisionPublicationCause(frame.legacy_only ? 1 : 0);
  if (frame.simulation_due && !frame.legacy_opportunity)
    ++PlayerLocomotionCadenceSelectionSuppressed();
}

void Player::SelectActionForFrame(ActionTickFrame& frame) {
  if (!frame.legacy_opportunity) return;

  PlayerCommandQueue commandQueue;      // selection queue
  std::vector<PlayerPathSelectionCommandProvenance> commandProvenance;
  bool uses_player_decision_queue = false;

  const bool trip_local_queue =
      interruptAnim == e_InterruptAnim_Trip && tripType != 0;
  if (frame.has_request_input) {
    commandQueue = std::move(frame.request_command_queue);
    for (std::size_t i = 0; i < commandQueue.size(); ++i)
      commandProvenance.push_back(
          PlayerPathSelectionCommandProvenance::Controller);
  } else if (trip_local_queue) {
    ++PlayerPathLocalTripAttempts();
    // ANIM-1 A7 step 2: the Player owns the local trip command construction.
    this->BuildTripSelectionCommands(
        commandQueue, tripDirection, tripType, legacy_spatial_state_.floatVelocity,
        legacy_spatial_state_.directionVec, legacy_spatial_state_.position);
    // All but the last command are trips; the last is the movement fallback.
    while (commandProvenance.size() + 1 < commandQueue.size()) {
      commandProvenance.push_back(
          PlayerPathSelectionCommandProvenance::LocalTrip);
    }
    tripType = 0;
    commandProvenance.push_back(
        PlayerPathSelectionCommandProvenance::LocalTripMovementFallback);
  } else {
    if (!this->HasPlayerDecisionQueue()) {
      ++PlayerDecisionClockQueueConsumersMissing();
      throw std::logic_error(
          "Player Decision queue missing before animation selection");
    }
    commandQueue = this->GetPlayerDecisionQueue();
    uses_player_decision_queue = true;
    for (size_t i = 0; i < commandQueue.size(); ++i)
      commandProvenance.push_back(
          PlayerPathSelectionCommandProvenance::Controller);
  }

  // iterate through the command queue and pick the first that is applicable
  bool found = false;
  bool preferPassAndShot = false; // pass/shot and such; in that case we want
                                  // trap/ballcontrol anims to be less prefered
  for (unsigned int i = 0; i < commandQueue.size(); i++) {
    const PlayerCommand &command = commandQueue[i];
    const PlayerPathSelectionCommandProvenance provenance =
        commandProvenance[i];

    if (command.desiredFunctionType == e_FunctionType_ShortPass ||
        command.desiredFunctionType == e_FunctionType_LongPass ||
        command.desiredFunctionType == e_FunctionType_HighPass ||
        command.desiredFunctionType == e_FunctionType_Shot) {
      preferPassAndShot = true;
    }
    found = this->TrySelectAction(
        frame.now, *frame.tick, command, *frame.observation, interruptAnim,
        preferPassAndShot, allowPreTouchRotationSmuggle,
        useContinuousBallCheck);
    if (found) {
      if (provenance == PlayerPathSelectionCommandProvenance::LocalTrip) {
        ++PlayerPathLocalTripSelected();
      } else if (provenance ==
                 PlayerPathSelectionCommandProvenance::LocalTripMovementFallback) {
        ++PlayerPathLocalTripMovementFallbackSelected();
        // Selection only: the local fallback used to write the compatibility
        // movement slot, but it never decided movement, so it no longer does.
      }
      break;
    }
  }

  if (interruptAnim == e_InterruptAnim_Switch && !found) {
    const auto logger = bq::log::get_log_by_name("football");
    if (logger.is_valid()) {
      logger.warning(
          "Player::RunActionTick: no applicable animation; current type {}",
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
          legacy_spatial_state_.floatVelocity,
          static_cast<float>(legacy_spatial_state_.bodyAngle),
          static_cast<float>(legacy_spatial_state_.relBodyAngle),
          GetCurrentBakedClip().metadata.outgoing_special_state);
    }
    throw std::runtime_error(
        "Player::RunActionTick: no applicable animation");
  }

  if (uses_player_decision_queue) {
    frame.decision_queue_selection_movement =
        found && frame.action->type == e_FunctionType_Movement;
  }
  if (found) {
    startPos = legacy_spatial_state_.position;
    startAngle = legacy_spatial_state_.angle;

    CalculatePredictedSituation(nextStartPos, nextStartAngle);

    // decaying difficulty
    float animDiff = GetCurrentBakedClip().metadata.difficulty;
    if (animDiff > decayingDifficultyFactor) decayingDifficultyFactor = animDiff;

    // if we just requeued, for example, from movement to ballcontrol, there's
    // no reason we can not immediately requeue to another ballcontrol again
    // (next time). only apply the initial requeue delay on subsequent anims of
    // the same type (so we can have a fast ballcontrol -> ballcontrol requeue,
    // but after that, use the initial delay)
    if (interruptAnim == e_InterruptAnim_ReQueue &&
        previousAnim_functionType == frame.action->type) {
      reQueueDelayFrames = initialReQueueDelayFrames;
    }
  }
  // 4f-a1: animation-owned query pressure. Every count below is a tick the
  // animation requeue reached the decision phase on with no cadence due; the
  // target is that this number, and the queries it causes, fall to zero.
  if (frame.legacy_only) {
    ++LegacyOnlyDecisionOpportunities();
    if (!found) {
      ++LegacyOnlyDecisionNoSelection();
    } else if (frame.action->type == e_FunctionType_Movement) {
      ++LegacyOnlyDecisionMovementSelections();
    } else {
      ++LegacyOnlyDecisionNonMovementSelections();
    }
  }
}

void Player::PublishCadenceLocomotion(ActionTickFrame& frame) {
  // Movement publication belongs to the locomotion cadence, not animation
  // opportunities. A simultaneous Trip-local selection does not change the
  // cadence publisher's source: it always samples the serialized decision queue.
  if (frame.simulation_due && !frame.movement_published_this_tick) {
    if (!this->HasPlayerDecisionQueue()) {
      ++PlayerDecisionClockQueueConsumersMissing();
      throw std::logic_error(
          "Player Decision queue missing before locomotion publication");
    }
    this->NoteDecisionMovementSelection(
        frame.decision_queue_selection_movement);
    if (this->PublishMovementIntentFromQueue(
            this->GetPlayerDecisionQueue(), frame.now,
            frame.ball_retainer == this)) {
      frame.movement_published_this_tick = true;
      ++PlayerPathDirectPublications();
      this->CommitLocomotionIntentRefresh(
          frame.tick->ball, frame.now, frame.ball_retainer == this);
      ++PlayerPathRefreshCommits();
    } else {
      ++PlayerPathCandidatesMissing();
    }
  }
  reQueueDelayFrames = std::max(reQueueDelayFrames - 1, 0);

  interruptAnim = e_InterruptAnim_None;
}

void Player::ApplyPostActionTick(ActionTickFrame& frame) {
  frame.ball_retainer = ProcessActionTouches(
      frame.now, *frame.tick, *frame.observation, frame.ball_retainer,
      *frame.touch_sink, *frame.runtime_sink);
  // ANIM-1 A9 step 2: the action/movement/rotation smuggle evolution is a pure
  // PlayerMotion calculation; this applies its deltas in the legacy order.
  ActionSmuggleTickInput smuggle_input;
  smuggle_input.frame_num = legacy_anim_state_.frameNum;
  // ANIM-2 A18.4: the action's entry frame, so the smuggle is spread over the
  // frames that actually remain.
  smuggle_input.start_frame = installed_action_.start_frame;
  smuggle_input.touch_frame = legacy_anim_state_.touchFrame;
  smuggle_input.frame_count = static_cast<int>(GetCurrentBakedClip().frame_count);
  smuggle_input.action_smuggle = legacy_anim_state_.actionSmuggle;
  smuggle_input.movement_smuggle = legacy_anim_state_.movementSmuggle;
  smuggle_input.rotation_smuggle_begin = legacy_anim_state_.rotationSmuggle.begin;
  smuggle_input.rotation_smuggle_end = legacy_anim_state_.rotationSmuggle.end;
  smuggle_input.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
  const ActionSmuggleTickResult smuggle =
      PlayerActionMotion::ActionSmuggleTick(smuggle_input);

  legacy_spatial_state_.actionSmuggleMovement = smuggle.action_smuggle_movement;
  legacy_anim_state_.actionSmuggleOffset += smuggle.action_smuggle_offset_delta;
  legacy_spatial_state_.movementSmuggleMovement = smuggle.movement_smuggle_movement;
  legacy_anim_state_.movementSmuggleOffset += smuggle.movement_smuggle_offset_delta;
  legacy_anim_state_.rotationSmuggleOffset = smuggle.rotation_smuggle_offset;

  // ANIM-1 A9 step 3: the goalkeeper boundary clamps are Player-owned.
  this->ApplyGoalkeeperBoundaryClamps(*frame.tick, frame.ball_retainer);

  // ANIM-1 A5: the Player owns the final pose. The action tick only hands over
  // the selected clip/frame and the base yaw once per tick; the Player's
  // playback state samples/blends it.
  // ANIM-1 A6.3c.3: the clip id and frame come from the Player authority.
  this->UpdatePlaybackFromAuthority(
      frame.now, startAngle + legacy_anim_state_.rotationSmuggleOffset,
      football::animation::BlendTicksForSwitch(
          legacy_anim_state_.functionType == e_FunctionType_Movement &&
          this->IsEligibleForProceduralLocomotion(
              frame.ball_retainer == this)));
}

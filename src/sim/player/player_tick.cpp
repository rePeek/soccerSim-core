// ANIM-1 A9.5-A9.8: the Player-owned action tick, state projection and the
// legacy selection support that used to live in Humanoid/HumanoidBase.
//
// The bodies here are verbatim moves from humanoidbase.cpp (and the
// non-touch part of Humanoid::Process); only the receiver changed, so the
// operation order, floating-point expressions and RNG draws are unchanged.
#include "sim/player/player.hpp"

#include <bq_log/bq_log.h>

#include "football/ball/ball.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/player/player_control_builder.hpp"

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
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_touch_math.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/player/player_runtime_sink.hpp"

using namespace football::math;
using football::ball::Ball;
using std::placeholders::_1;
using std::placeholders::_2;

constexpr bool animSmoothing = true;
constexpr bool useContinuousBallCheck = true;
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle = animSmoothing ? 0.25f * pi : 0.0f;
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

const AnimationClip &Player::GetBakedClip(AnimationId id) const {
  return animations_.Get(static_cast<uint32_t>(id));
}

football::sim::SelectedAction Player::CurrentSelectedAction() const {
  football::sim::SelectedAction selection;
  selection.clip_id = anim_state_.animationId;
  selection.function_type = anim_state_.functionType;
  selection.start_frame = anim_state_.frameNum;
  selection.frame_count = static_cast<int>(GetBakedClip(anim_state_.animationId).frame_count);
  selection.touch_frame = anim_state_.touchFrame;
  selection.touch_position = anim_state_.touchPos;
  return selection;
}

const AnimationClip &Player::GetCurrentBakedClip() const {
  return GetBakedClip(anim_state_.animationId);
}


void Player::MirrorLegacyState() {
  // Mirrors the field-level legacy contract, not a whole-state coordinate
  // transform: SpatialState::Mirror negates the movement fields but leaves the
  // facing/body fields untouched. Player::Mirror() mirrors its kinematic
  // mirror with exactly the same asymmetry, and the movement oracle verifies
  // the result bit-exactly.
  startPos.Mirror();
  startAngle.Mirror();
  nextStartPos.Mirror();
  nextStartAngle.Mirror();
  spatial_state_.Mirror();
  previousPosition2D.Mirror();
  tripDirection.Mirror();
  decayingPositionOffset.Mirror();
  predicate_RelDesiredDirection.Mirror();
}



void Player::ResetLegacyPosition(const Vector3 &newPos,
                                 const Vector3 &focusPos) {

  startPos = newPos;
  startAngle = FixAngle((focusPos - newPos).GetNormalized(Vector3(0, -1, 0)).GetAngle2D());
  nextStartPos = startPos;
  nextStartAngle = startAngle;
  previousPosition2D = startPos;

  spatial_state_.position = startPos;
  spatial_state_.angle = startAngle;
  spatial_state_.directionVec = Vector3(0, -1, 0).GetRotated2D(startAngle);
  spatial_state_.floatVelocity = 0;

  spatial_state_.movement = Vector(0);
  spatial_state_.actionSmuggleMovement = Vector(0);
  spatial_state_.movementSmuggleMovement = Vector(0);
  spatial_state_.positionOffsetMovement = Vector(0);
  spatial_state_.relBodyAngleNonquantized = 0;

  spatial_state_.enumVelocity = e_Velocity_Idle;
  spatial_state_.floatVelocity = 0;
  spatial_state_.movement = Vector3(0);
  spatial_state_.relBodyDirectionVec = Vector3(0, -1, 0);
  spatial_state_.relBodyAngle = 0;
  // After a reset the body faces the focus direction, matching the locomotion
  // direction and the animation base angle (startAngle). Otherwise the derived
  // GetFacing() (velocity=0 -> bodyFacing) would report world -Y instead of the
  // direction the reset actually asked for. relBodyDirectionVec/relBodyAngle
  // stay (0,-1,0)/0 because body and locomotion now agree.
  spatial_state_.bodyDirectionVec = spatial_state_.directionVec;
  spatial_state_.bodyAngle = spatial_state_.angle;
  spatial_state_.foot = e_Foot_Right;

  const AnimationId idleAnimID = SelectIdleMovementAnim(
      animations_, spatial_state_.position, spatial_state_.angle,
      spatial_state_.floatVelocity);
  anim_state_.animationId = idleAnimID;
  anim_state_.positions.clear();
  anim_state_.positions = GetBakedClip(anim_state_.animationId).root_positions;
  anim_state_.frameNum =
      rng_.Uniform(0, static_cast<int>(GetCurrentBakedClip().frame_count) - 2);
  anim_state_.touchFrame = -1;
  anim_state_.actionSmuggle = Vector3(0);
  anim_state_.actionSmuggleOffset = Vector3(0);
  anim_state_.actionSmuggleSustain = Vector3(0);
  anim_state_.actionSmuggleSustainOffset = Vector3(0);
  anim_state_.movementSmuggle = Vector3(0);
  anim_state_.movementSmuggleOffset = Vector3(0);
  anim_state_.rotationSmuggle.begin = 0;
  anim_state_.rotationSmuggle.end = 0;
  anim_state_.rotationSmuggleOffset = 0;
  anim_state_.functionType = e_FunctionType_Movement;
  anim_state_.incomingMovement = Vector3(0);
  anim_state_.outgoingMovement = Vector3(0);
  anim_state_.positionOffset = Vector3(0);

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
  spatial_state_.position += offset;
  spatial_state_.positionOffsetMovement += offset * 100.0f;
  decayingPositionOffset += offset;
  if (decayingPositionOffset.GetLength() > 0.1f) decayingPositionOffset = decayingPositionOffset.GetNormalized() * 0.1f;
  anim_state_.positionOffset += offset;
}



void Player::ResetLegacySituation(const Vector3 &focusPos) {
  mentalImageTime = std::chrono::milliseconds{0};
  ResetLegacyPosition(spatial_state_.position, focusPos);
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

  if (anim_state_.positions.size() > (unsigned int)anim_state_.frameNum) {
    assert(anim_state_.positions.size() > (unsigned int)(static_cast<int>(GetCurrentBakedClip().frame_count) - 1));
    predictedPos = spatial_state_.position + anim_state_.positions.at((static_cast<int>(GetCurrentBakedClip().frame_count) - 1)) + anim_state_.actionSmuggle + anim_state_.actionSmuggleSustain + anim_state_.movementSmuggle;
  } else {
    predictedPos = spatial_state_.position + GetCurrentBakedClip().metadata.translation.Get2D().GetRotated2D(spatial_state_.angle) + anim_state_.actionSmuggle + anim_state_.actionSmuggleSustain + anim_state_.movementSmuggle;
  }

  predictedAngle = spatial_state_.angle + GetCurrentBakedClip().metadata.outgoing_angle + anim_state_.rotationSmuggle.end;
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
    if (anim_state_.frameNum > 12) {
      spatial_state_.foot = static_cast<e_Foot>(GetCurrentBakedClip().metadata.outgoing_foot);
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
  spatial_state_.position = state.position;
  spatial_state_.movement = state.velocity;
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d1: these serialized legacy aliases have no pure-locomotion producer.
    // Project the authoritative velocity rather than retaining animation history.
  }
  spatial_state_.directionVec = state.GetFacing();
  spatial_state_.floatVelocity = spatial_state_.movement.GetLength();
  spatial_state_.enumVelocity = FloatToEnumVelocity(spatial_state_.floatVelocity);
  spatial_state_.angle =
      ModulateIntoRange(-pi, pi, FixAngle(spatial_state_.directionVec.GetAngle2D()));
  ApplySimulationBodyState(state);
  // The next tick derives its raw movement from this, so it must describe the
  // position actually in force. Collision corrections land here too, exactly
  // as they did when the legacy root motion owned the position.
  previousPosition2D = spatial_state_.position;
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
  spatial_state_.bodyDirectionVec = state.bodyFacing;
  spatial_state_.bodyAngle =
      spatial_state_.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  const Vector3 relative = state.bodyFacing.GetRotated2D(-spatial_state_.angle)
                               .GetNormalized(Vector3(0, -1, 0));
  spatial_state_.relBodyAngleNonquantized =
      relative.GetAngle2D(Vector3(0, -1, 0));

  // Compatibility for animation selection only. This quantization is a leaf:
  // bodyDirectionVec remains the continuous simulation projection above.
  spatial_state_.relBodyDirectionVec =
      ForceIntoAllowedBodyDirectionVec(relative);
  spatial_state_.relBodyAngle =
      spatial_state_.relBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
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
  // change what this tick's locomotion was. Writing spatial_state_.position here
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
  // is unchanged (see PlayerMotion::BuildActionMotionPlan).
  const AnimationClip &clip = GetBakedClip(animID);

  ActionMotionInput input;
  input.spatial_movement = spatial_state_.movement;
  input.angle = spatial_state_.angle;
  input.direction_vec = spatial_state_.directionVec;
  input.float_velocity = spatial_state_.floatVelocity;
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

  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);

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
                           std::span<MentalImage> history,
                           football::sim::AcceptedTouchSink& touch_sink,
                           football::sim::PlayerRuntimeSink& runtime_sink) {
  if (startPos.coords[2] != 0.f) {
    throw std::logic_error("Player::RunActionTick: player position must have zero height");
  }
  this->NoteProcessedPlayerTick();
  // ANIM-1 A7: apply the passive trip the Player accepted at the previous
  // contact phase, exactly where the legacy direct interrupt write took effect.
  if (const auto pending_trip = this->ConsumePendingPassiveTrip()) {
    interruptAnim = e_InterruptAnim_Trip;
    tripDirection = pending_trip->direction;
    tripType = pending_trip->trip_type;
  }
  auto currentMentalImage = football::sim::observation::SampleMentalImage(history, mentalImageTime);
  // this might be the solution to long-term imbalance
  decayingPositionOffset *= 0.95f;
  if (decayingPositionOffset.GetLength() < 0.005) decayingPositionOffset.Set(0);
  decayingDifficultyFactor = clamp(decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);


  Player* ball_retainer = tick.ball_retainer;

  bool instaDoorheb = false;
  if (tick.touches.last_team == this->GetTeamID()) instaDoorheb = true;
  mentalImageTime = std::chrono::milliseconds{instaDoorheb ? 0 : this->GetReactionTime_ms()};

  // The authoritative movement state at tick start. Captured before
  // CalculateSpatialState so the procedural model integrates from the world
  // state in force, not from the animation prediction for this tick.
  const PlayerKinematicState tickStartState = GetKinematicState();
  // gate-mismatch-context: exactly one attribution per real Player tick, before
  // any of this tick's CalculateSpatialState/ProjectMovementState evaluations.
  // Gameplay continuity advances before the audit observes this tick, so the
  // epoch is decided by eligibility rather than by telemetry bookkeeping.
  this->AdvanceLocomotionContinuity(
      this->IsEligibleForProceduralLocomotion(ball_retainer == this));
  // re-entry audit must see every real player tick, eligible or not, because
  // leaving locomotion is what arms the next re-entry classification.
  this->NoteLocomotionReentryTick(
      this->IsEligibleForProceduralLocomotion(ball_retainer == this),
      this->IsLocomotionIntentRefreshDue(
          now),
      now);
  // The legacy gate-versus-compatibility-source measurement lived here. Execution
  // authority is now the decision intent, so comparing the animation command
  // against the decision clock measures two compatibility slots and says nothing
  // about what executes.

  // At most one Movement publication per real-player tick, including a prior
  // continuity repair publication before execution.
  bool movement_published_this_tick = false;

  // 4f-a3a: the complete decision queue has its own simulation clock. Its
  // cadence depends on world context, never on animation requeue opportunities.
  const auto decision_now = now;
  const int decision_now_ms = static_cast<int>(football::sim::ToMilliseconds(decision_now));
  const float decision_distance_to_ball =
      (tick.ball.state().position.Get2D() - tickStartState.position).GetLength();
  const bool designated_possession_player =
      tick.designated_possession_player == this;
  const bool designated_team_possession_player =
      tick.own_team.DesignatedPlayer() == this;
  const auto player_decision_cadence = PlayerDecisionCadenceForContext(
      designated_possession_player, designated_team_possession_player,
      decision_distance_to_ball);
  const bool continuity_repair_due =
      this->IsEligibleForProceduralLocomotion(ball_retainer == this) &&
      this->DecisionLocomotionEpochIsStale();
  const bool player_decision_due =
      this->IsPlayerDecisionRefreshDue(
          decision_now, player_decision_cadence);
  if (continuity_repair_due || player_decision_due) {
    PlayerCommandQueue player_decision_commands;
    this->RequestCommand(player_decision_commands,
        {tick.ball, tick.touches, tick.restart,
         ball_retainer, tick.pitch, tick.own_team});
    this->PublishPlayerDecisionQueue(
        player_decision_commands, decision_now);
    bool has_movement = false;
    for (const PlayerCommand &command : player_decision_commands)
      has_movement |= command.desiredFunctionType == e_FunctionType_Movement &&
                      command.useDesiredMovement;
    this->NoteControllerQuery(has_movement, decision_now, ball_retainer == this);
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
            this->GetPlayerDecisionQueue(), decision_now, ball_retainer == this)) {
      movement_published_this_tick = true;
      this->CommitLocomotionIntentRefresh(tick.ball, decision_now, ball_retainer == this);
      ++ContinuityRepairPublications();
    } else {
      ++ContinuityRepairCandidatesMissing();
    }
    this->NoteDecisionPublicationCause(0);
  }

  CalculateSpatialState(ball_retainer);
  spatial_state_.positionOffsetMovement = Vector3(0);
  // H3e1c-3c: pure locomotion ticks are produced by the simulation; every
  // other tick keeps the legacy root motion. Either way the legacy movement
  // fields end up as a projection of the authoritative kinematic state.
  ProjectMovementState(tickStartState, ball_retainer, tick.restart_needs_simulation);

  this->AdvanceActionTick();

  const PlayerActionState &action =
      this->GetSimulationActionState();


  /*

    bump if (anim_state_.positionOffset.GetLength() > 0.1f && interruptAnim ==
    e_InterruptAnim_None) { interruptAnim = e_InterruptAnim_Bump;
    }
  */

  if (action.IsAtLastFrame() && interruptAnim == e_InterruptAnim_None) {
    interruptAnim = e_InterruptAnim_Switch;
  }

  bool mayReQueue = allowReQueue;

  // already some anim interrupt waiting?

  if (mayReQueue) {
    if (interruptAnim != e_InterruptAnim_None) {
      mayReQueue = false;
    }
  }

  // may requeue on this frame?

  /* can't do this: requeued movement anims should be requeueable into touch
    anims if (mayReQueue) { // never requeue a requeued anim if
    (anim_state_.originatingInterrupt != e_Interrupt_None &&
          anim_state_.originatingInterrupt != e_Interrupt_Switch) {
     mayRequeue = false;
    }*/

  if (mayReQueue) {
    bool frameNumPredicate = false;
    float actionDistance = ((spatial_state_.position + spatial_state_.movement * 0.1f) - tick.ball.Predict(football::sim::TickSpan{10}).position.Get2D()).GetLength();

    int team_id = tick.processing_slot;
    using namespace football::sim::player_timing;
    const auto phase = static_cast<std::uint64_t>(team_id);
    if (tick.designated_possession_player == this &&
        actionDistance < 3.0f) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwnerNear, football::sim::TickSpan{phase});

    } else if (tick.designated_possession_player == this) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwner, football::sim::TickSpan{phase});

    } else if (tick.own_team.DesignatedPlayer() == this) {
      frameNumPredicate = StaggeredRefreshDue(now, kTeamOwner, football::sim::TickSpan{phase * 2});

    } else if (actionDistance < 5.0f) {
      frameNumPredicate = StaggeredRefreshDue(now, kNearBall, football::sim::TickSpan{phase * 2});

    } else if (actionDistance < 10.0f) {
      frameNumPredicate = StaggeredRefreshDue(now, kApproachingBall, football::sim::TickSpan{phase * 4});
    }

    if (!frameNumPredicate) mayReQueue = false;
  }

  // right anim to requeue?

  if (mayReQueue) {

    float ballDistance = (currentMentalImage->GetBallPrediction(500, now, tick.ball).Get2D() - spatial_state_.position).GetLength();
    if (((action.type == e_FunctionType_Movement &&
            !this->HasPossession() && ballDistance < 16.0f) ||
           (action.type == e_FunctionType_Movement &&
            this->HasPossession()) ||  // passes / shot
           (action.type == e_FunctionType_Trap && TouchPending()) ||
           (action.type == e_FunctionType_BallControl &&
            TouchPending())) &&
        /* now done later on, else we can't requeue to pass/shot during
        trap/ballcontrol (action.type == e_FunctionType_Trap &&
        TouchPending() && allowTrapReQueue && action.Frame() <=
        maxTrapReQueueFrame) || (action.type ==
        e_FunctionType_BallControl && TouchPending() && allowBallControlReQueue
        && action.Frame() <= maxBallControlReQueueFrame) ) &&
        */
        GetCurrentBakedClip().metadata.incoming_special_state.empty() &&
        GetCurrentBakedClip().metadata.outgoing_special_state.empty()) {
      mayReQueue = true;
    } else {
      mayReQueue = false;
    }
  }

  // okay, see if we need to requeue

  if (mayReQueue) {
    interruptAnim = e_InterruptAnim_ReQueue;
  }

  const bool legacy_opportunity = interruptAnim != e_InterruptAnim_None;
  const bool simulation_due =
      this->NoteLocomotionIntentCadence(legacy_opportunity, decision_now, ball_retainer == this);
  // Tag the cause of any publication on this path, so a legacy-driven query is
  // not credited to the simulation cadence in the audit.
  const bool legacy_only = legacy_opportunity && !simulation_due;
  this->NoteDecisionPublicationCause(legacy_only ? 1 : 0);
  bool decision_queue_selection_movement = false;
  if (simulation_due && !legacy_opportunity)
    ++PlayerLocomotionCadenceSelectionSuppressed();
  if (legacy_opportunity) {
    PlayerCommandQueue commandQueue;      // selection queue
    std::vector<PlayerPathSelectionCommandProvenance> commandProvenance;
    bool uses_player_decision_queue = false;

    const bool trip_local_queue =
        interruptAnim == e_InterruptAnim_Trip && tripType != 0;
    if (trip_local_queue) {
      ++PlayerPathLocalTripAttempts();
      // ANIM-1 A7 step 2: the Player owns the local trip command construction.
      this->BuildTripSelectionCommands(
          commandQueue, tripDirection, tripType, spatial_state_.floatVelocity,
          spatial_state_.directionVec, spatial_state_.position);
      // All but the last command are trips; the last is the movement fallback.
      while (commandProvenance.size() + 1 < commandQueue.size()) {
        commandProvenance.push_back(PlayerPathSelectionCommandProvenance::LocalTrip);
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
        commandProvenance.push_back(PlayerPathSelectionCommandProvenance::Controller);
    }

    // iterate through the command queue and pick the first that is applicable

    bool found = false;
    bool preferPassAndShot = false; // pass/shot and such; in that case we want trap/ballcontrol anims to be less prefered
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
      found = this->TrySelectAction(now, tick, command, history, interruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
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
        logger.warning("Player::RunActionTick: no applicable animation; current type {}",
                       GetCurrentBakedClip().metadata.action_type);
        for (const auto& command : commandQueue) {
          logger.warning("desired type {}, velocity {}, direction ({}, {}, {})",
                         command.desiredFunctionType, command.desiredVelocityFloat,
                         command.desiredDirection.coords[0],
                         command.desiredDirection.coords[1],
                         command.desiredDirection.coords[2]);
        }
        logger.warning("current velocity {}, body angle {}, relative angle {}, state {}",
                       spatial_state_.floatVelocity, static_cast<float>(spatial_state_.bodyAngle),
                       static_cast<float>(spatial_state_.relBodyAngle),
                       GetCurrentBakedClip().metadata.outgoing_special_state);
      }
      throw std::runtime_error("Player::RunActionTick: no applicable animation");
    }


    if (uses_player_decision_queue) {
      decision_queue_selection_movement =
          found && action.type == e_FunctionType_Movement;
    }
    if (found) {
      startPos = spatial_state_.position;
      startAngle = spatial_state_.angle;

      CalculatePredictedSituation(nextStartPos, nextStartAngle);

      // decaying difficulty
      float animDiff = GetCurrentBakedClip().metadata.difficulty;
      if (animDiff > decayingDifficultyFactor) decayingDifficultyFactor = animDiff;

      // if we just requeued, for example, from movement to ballcontrol, there's no reason we can not immediately requeue to another ballcontrol again (next time). only apply the initial requeue delay on subsequent anims of the same type
      // (so we can have a fast ballcontrol -> ballcontrol requeue, but after that, use the initial delay)
      if (interruptAnim == e_InterruptAnim_ReQueue &&
          previousAnim_functionType == action.type) {
        reQueueDelayFrames = initialReQueueDelayFrames; // don't try requeueing (some types of anims, see selectanim()) too often
      }
    }
    // 4f-a1: animation-owned query pressure. Every count below is a tick the
    // animation requeue reached the decision phase on with no cadence due; the
    // target is that this number, and the queries it causes, fall to zero.
    if (legacy_only) {
      ++LegacyOnlyDecisionOpportunities();
      if (!found) {
        ++LegacyOnlyDecisionNoSelection();
      } else if (action.type == e_FunctionType_Movement) {
        ++LegacyOnlyDecisionMovementSelections();
      } else {
        ++LegacyOnlyDecisionNonMovementSelections();
      }
    }
  }

  // Movement publication belongs to the locomotion cadence, not animation
  // opportunities. A simultaneous Trip-local selection does not change the
  // cadence publisher's source: it always samples the serialized decision queue.
  if (simulation_due && !movement_published_this_tick) {
    if (!this->HasPlayerDecisionQueue()) {
      ++PlayerDecisionClockQueueConsumersMissing();
      throw std::logic_error(
          "Player Decision queue missing before locomotion publication");
    }
    this->NoteDecisionMovementSelection(
        decision_queue_selection_movement);
    if (this->PublishMovementIntentFromQueue(
            this->GetPlayerDecisionQueue(), decision_now, ball_retainer == this)) {
      movement_published_this_tick = true;
      ++PlayerPathDirectPublications();
      this->CommitLocomotionIntentRefresh(tick.ball, decision_now, ball_retainer == this);
      ++PlayerPathRefreshCommits();
    } else {
      ++PlayerPathCandidatesMissing();
    }
  }
  reQueueDelayFrames = std::max(reQueueDelayFrames - 1, 0);

  interruptAnim = e_InterruptAnim_None;
  ball_retainer = ProcessActionTouches(now, tick, history, ball_retainer,
                                       touch_sink, runtime_sink);
  // ANIM-1 A9 step 2: the action/movement/rotation smuggle evolution is a pure
  // PlayerMotion calculation; this applies its deltas in the legacy order.
  ActionSmuggleTickInput smuggle_input;
  smuggle_input.frame_num = anim_state_.frameNum;
  smuggle_input.touch_frame = anim_state_.touchFrame;
  smuggle_input.frame_count = static_cast<int>(GetCurrentBakedClip().frame_count);
  smuggle_input.action_smuggle = anim_state_.actionSmuggle;
  smuggle_input.movement_smuggle = anim_state_.movementSmuggle;
  smuggle_input.rotation_smuggle_begin = anim_state_.rotationSmuggle.begin;
  smuggle_input.rotation_smuggle_end = anim_state_.rotationSmuggle.end;
  smuggle_input.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
  const ActionSmuggleTickResult smuggle =
      PlayerMotion::ActionSmuggleTick(smuggle_input);

  spatial_state_.actionSmuggleMovement = smuggle.action_smuggle_movement;
  anim_state_.actionSmuggleOffset += smuggle.action_smuggle_offset_delta;
  spatial_state_.movementSmuggleMovement = smuggle.movement_smuggle_movement;
  anim_state_.movementSmuggleOffset += smuggle.movement_smuggle_offset_delta;
  anim_state_.rotationSmuggleOffset = smuggle.rotation_smuggle_offset;

  // ANIM-1 A9 step 3: the goalkeeper boundary clamps are Player-owned.
  this->ApplyGoalkeeperBoundaryClamps(tick, ball_retainer);

  // next frame

  if (anim_state_.positions.size() > (unsigned int)anim_state_.frameNum) {
    //printf("size: %i\n", anim_state_.positions.size());
  } else {
  }

  // ANIM-1 A5: the Player owns the final pose. The action tick only hands over the
  // selected clip/frame and the base yaw once per tick; the Player's playback
  // state samples/blends it.
  // ANIM-1 A6.3c.3: the clip id and frame come from the Player authority.
  this->UpdatePlaybackFromAuthority(
      now, startAngle + anim_state_.rotationSmuggleOffset,
      football::animation::BlendTicksForSwitch(anim_state_.functionType == e_FunctionType_Movement &&
                          this->IsEligibleForProceduralLocomotion(ball_retainer == this)));
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
  if (clip.poses.empty() || anim_state_.frameNum < 0)
    throw std::logic_error("missing runtime baked pose frame");
  const auto frame = std::min(std::size_t(anim_state_.frameNum), clip.poses.size() - 1);
  PlayerBakedPoseSample sample;
  sample.pose = clip.poses[frame];
  sample.state = state;
  sample.animation_id = anim_state_.animationId;
  sample.requested_frame = anim_state_.frameNum;
  sample.sampled_frame = int(frame);
  sample.frame_clamped = sample.requested_frame != sample.sampled_frame;
  sample.base_angle = startAngle + anim_state_.rotationSmuggleOffset;
  sample.body_profile = profile;
  sample.action = anim_state_.functionType;
  sample.outgoing_special_state = !clip.metadata.outgoing_special_state.empty();
  return sample;
}

// ANIM-2 A11.0: a read-only pose preview. It never advances the action clock,
// draws RNG or mutates the Player. For a positive offset it returns the raw
// clip skeleton at the requested future frame (not the blended pose), which is
// exactly the body-forecast input the Simulation used to build by indexing the
// library itself.
PlayerBakedPoseSample Player::PreviewPose(int frame_offset) const {
  PlayerBakedPoseSample sample = GetBakedPoseSample();
  const auto& clip =
      animations_.Get(static_cast<std::uint32_t>(sample.animation_id));
  sample.requested_frame += frame_offset;
  sample.sampled_frame =
      std::min(sample.requested_frame, static_cast<int>(clip.poses.size()) - 1);
  sample.frame_clamped = sample.sampled_frame != sample.requested_frame;
  sample.pose = clip.poses[static_cast<std::size_t>(sample.sampled_frame)];
  sample.outgoing_special_state = !clip.metadata.outgoing_special_state.empty();
  return sample;
}

void Player::ProjectSelectedActionCompat(
    int selectedAnimID, const football::sim::TouchChoice &touch_choice,
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, std::span<MentalImage> history,
    e_InterruptAnim localInterruptAnim, bool material_candidate) {
  const PlayerActionState &action = this->GetSimulationActionState();
  previousAnim_functionType = anim_state_.functionType;

  anim_state_.animationId = selectedAnimID;
  anim_state_.functionType = command.desiredFunctionType;
  anim_state_.frameNum = 0;
  anim_state_.touchFrame = touch_choice.touch_frame;
  anim_state_.touchPos = touch_choice.touch_position;
  anim_state_.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, spatial_state_.relBodyAngleNonquantized - GetCurrentBakedClip().metadata.incoming_body_angle) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (anim_state_.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
  anim_state_.rotationSmuggle.end = touch_choice.rotation_smuggle;
  anim_state_.rotationSmuggleOffset = 0;
  anim_state_.actionSmuggle = touch_choice.action_smuggle;
  anim_state_.actionSmuggleOffset = Vector3(0);
  anim_state_.actionSmuggleSustain = Vector3(0); // calculated below
  anim_state_.actionSmuggleSustainOffset = Vector3(0);
  anim_state_.movementSmuggle = Vector3(0); // needs to be reset here, else the previous calc is used in upcoming 'calculatemovementsmuggle'
  anim_state_.movementSmuggleOffset = Vector3(0);
  anim_state_.incomingMovement = spatial_state_.movement;
  anim_state_.outgoingMovement = CalculateOutgoingMovement(touch_choice.positions);
  anim_state_.positions.clear();
  anim_state_.positions.assign(touch_choice.positions.begin(), touch_choice.positions.end());
  anim_state_.positionOffset = Vector3(0);
  anim_state_.originatingCommand = command;
  RecordMovementCommandAcceptance(material_candidate, action.type,
                                  static_cast<int>(football::sim::ToMilliseconds(action.elapsed)), localInterruptAnim,
                                  command, static_cast<int>(GetCurrentBakedClip().frame_count));
  // Must run after the anim_state_ field writes above (it reads the new
  // animationId/positions/touchFrame and GetCurrentBakedClip), before install.
  anim_state_.movementSmuggle = this->ComputeMovementSmuggle(now, tick, command.desiredDirection, command.desiredVelocityFloat, history);
  anim_state_.movementSmuggleOffset = Vector3(0);
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

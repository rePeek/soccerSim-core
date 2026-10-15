#include "sim/player/player_active_touch_shadow.hpp"
#include "sim/player/player_active_touch_model.hpp"
// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include <algorithm>
#include <functional>
#include <bq_log/bq_log.h>
#include <cassert>
#include <stdexcept>
#include "sim/player/humanoid/humanoid.hpp"
#include "sim/event/accepted_touch_sink.hpp"

#include <cmath>

#include "sim/player/humanoid/humanoid_utils.hpp"

#include "sim/player/player_retain_anchor.hpp"

#include "sim/player/player.hpp"
#include "sim/player/player_motion.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/player/locomotion_animation_strategy.hpp"
#include "football/animation/movement_selection.hpp"
#include "football/animation/select_movement.hpp"
#include "football/animation/touch_geometry.hpp"
#include "football/animation/touch_scoring.hpp"
#include <iterator>

#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/event/accepted_touch_sink.hpp"
#include "sim/player/player_runtime_sink.hpp"
#include "sim/player/player_motion_constants.hpp"


#include "sim/player/kick_targeting.hpp"

#include "football/animation/selector.hpp"
#include "football/animation/library.hpp"
#include "sim/event/touch_query.hpp"
#include "sim/referee/referee.hpp"


using std::placeholders::_1;
using std::placeholders::_2;
constexpr bool animSmoothing = true;
constexpr bool useContinuousBallCheck = true;

// 4b''-player-noquery-audit: provenance stays local to the selection queue.
// It is deliberately not part of PlayerCommand or serialized simulation state.
enum class PlayerPathSelectionCommandProvenance {
  Controller,
  LocalTrip,
  LocalTripMovementFallback
};
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle = animSmoothing ? 0.25f * pi : 0.0f;
constexpr int initialReQueueDelayFrames = 22;
constexpr bool allowReQueue = true;
constexpr bool allowPreTouchRotationSmuggle = false;

// ANIM-1 A6.4 step 2b: the single definition now lives in the Animation
// module (football/animation/touch_geometry.hpp).
using football::animation::SegmentProjectionParameter;

Humanoid::Humanoid(Player *player, const AnimationLibrary& animations, football::math::Rng& rng)
    : HumanoidBase(player, animations, rng) {
}

Humanoid::~Humanoid() {}

Player *Humanoid::CastPlayer() const { return player; }

bool _PassFiddlingEnabled() {
  return true;
}

void Humanoid::Process(football::sim::Tick now, const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::AcceptedTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
  // Reject invalid runtime state before the spatial/action debug oracles run.
  if (startPos.coords[2] != 0.f) {
    throw std::logic_error("Humanoid::Process: player position must have zero height");
  }
  CastPlayer()->NoteProcessedPlayerTick();
  // ANIM-1 A7: apply the passive trip the Player accepted at the previous
  // contact phase, exactly where the legacy direct Humanoid write took effect.
  if (const auto pending_trip = CastPlayer()->ConsumePendingPassiveTrip()) {
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
  std::optional<Vector3> prepared_velocity;
  std::optional<Vector3> prepared_anchor;
  const auto set_rotation = [&](radian, radian, radian, float) {
    // Legacy direct spin assignment is gone; spin comes from the point impulse.
  };
  // Tick-local publication: actors never reach the runtime owner for touch notification.
  std::optional<ActiveTouchObservation> active_observation;
  const auto begin_active_observation = [&](ActiveTouchOrigin origin) {
    if (!tick.active_touch_shadow) return;
    ActiveTouchObservation observation;
    const auto& state = CastPlayer()->GetSimulationActionState();
    observation.candidate.player = CastPlayer()->GetID();
    observation.candidate.action = state.type;
    const auto& part = GetCurrentBakedClip().metadata.touch_bodypart;
    observation.candidate.body_part = part.find("head") != std::string::npos ? PlayerBodyPart::Head :
        (part.find("foot") != std::string::npos || part.find("lowerleg") != std::string::npos ?
         PlayerBodyPart::LowerBody : PlayerBodyPart::UpperBody);
    observation.candidate.scheduled_tick = now + football::sim::TickSpan{1};
    observation.legacy_rule_tick = now;
    observation.animation_id = currentAnim.animationId;
    observation.contact_frame = state.ContactFrame();
    observation.elapsed = state.Frame();
    observation.origin = origin;
    observation.stage = ActiveTouchStage::NoImpulse;
    observation.before = tick.ball.state();
    observation.raw_animation_point = currentAnim.touchPos + currentAnim.positionOffset;
    observation.player_position = spatialState.position;
    active_observation = observation;
  };
  const auto flush_active_observation = [&] {
    if (!active_observation) return;
    active_observation->after = tick.ball.state();
    active_observation->candidate.target_velocity = active_observation->after.velocity;
    active_observation->candidate.target_angular_velocity = active_observation->after.angular_velocity;
    tick.active_touch_shadow->Observe(*active_observation);
    active_observation.reset();
  };
  const auto apply_touch = [&](const Vector3& velocity) {
    prepared_velocity = velocity;
  };
  const auto notify_touch = [&](e_TouchType type) {
    if (tick.touch_preparation) {
      if (prepared_velocity) {
        const auto& part = GetCurrentBakedClip().metadata.touch_bodypart;
        const auto body_part = part.find("head") != std::string::npos ? PlayerBodyPart::Head :
            (part.find("foot") != std::string::npos || part.find("lowerleg") != std::string::npos ?
             PlayerBodyPart::LowerBody : PlayerBodyPart::UpperBody);
        // P4f-3: capture the real boot surface point nearest the ball. Take
        // BOTH boot capsules and keep the nearer one: the kicking boot is the
        // one at the ball at the contact frame while the planted support boot
        // is elsewhere. `current_foot` is selector provenance, not a reliable
        // contact-foot identity in the shipped asset.
        football::math::Vector3 strike_point;
        float strike_radius = 0.0f;
        int strike_foot = -1;
        {
          const auto sample = CastPlayer()->GetBakedPoseSample();
          const auto regions = sample.ContactRegions();
          const auto& ball_center = tick.ball.state().position;
          const auto surface = [&](const auto& boot) {
            const auto axis = boot.b - boot.a;
            const float len2 = axis.GetDotProduct(axis);
            const float t = len2 > 0 ? clamp((ball_center - boot.a).GetDotProduct(axis)/len2, 0.0f, 1.0f) : 0.0f;
            const auto segment = boot.a + axis * t;
            const auto delta = ball_center - segment;
            const float dist = delta.GetLength();
            return dist > 1e-6f ? segment + delta * (boot.radius/dist) : segment;
          };
          const auto& left = std::get<football::geometry::Capsule>(regions[1]);
          const auto& right = std::get<football::geometry::Capsule>(regions[2]);
          const auto lp = surface(left), rp = surface(right);
          const float ld = (lp - ball_center).GetLength();
          const float rd = (rp - ball_center).GetLength();
          strike_foot = ld <= rd ? 0 : 1;
          const auto& boot = ld <= rd ? left : right;
          strike_point = ld <= rd ? lp : rp;
          strike_radius = boot.radius;
        }
        tick.touch_preparation->PrepareTouch({
            {now, CastPlayer()->GetID(), CastPlayer()->GetTeamSide(), type, tick.ball.state().position,
             *prepared_velocity, static_cast<int>(CastPlayer()->GetSimulationActionState().type)},
            body_part, prepared_anchor.value_or(currentAnim.touchPos + currentAnim.positionOffset),
            *prepared_velocity, prepared_anchor.has_value(),
            strike_point, strike_radius, strike_foot,
            currentAnim.animationId,
            CastPlayer()->GetSimulationActionState().ContactFrame(),
            CastPlayer()->GetSimulationActionState().Frame()});
      }
      prepared_velocity.reset();
      prepared_anchor.reset();
      return;
    }
  };
  // Prepared execution never commits physics here. The default fused path
  // retains its legacy touch until the reviewed default/Golden transition.
  const auto physical_touch = [&](e_FunctionType, const Vector3& touchVec) -> bool {
    if (!tick.touch_preparation) return false;
    apply_touch(touchVec);
    return true;
  };

  bool instaDoorheb = false;
  if (tick.touches.last_team == CastPlayer()->GetTeamID()) instaDoorheb = true;
  mentalImageTime = std::chrono::milliseconds{instaDoorheb ? 0 : CastPlayer()->GetReactionTime_ms()};

  // The authoritative movement state at tick start. Captured before
  // CalculateSpatialState so the procedural model integrates from the world
  // state in force, not from the animation prediction for this tick.
  const PlayerKinematicState tickStartState = player->GetKinematicState();
  // gate-mismatch-context: exactly one attribution per real Player tick, before
  // any of this tick's CalculateSpatialState/ProjectMovementState evaluations.
  // Gameplay continuity advances before the audit observes this tick, so the
  // epoch is decided by eligibility rather than by telemetry bookkeeping.
  CastPlayer()->AdvanceLocomotionContinuity(
      CastPlayer()->IsEligibleForProceduralLocomotion(ball_retainer == player));
  // re-entry audit must see every real player tick, eligible or not, because
  // leaving locomotion is what arms the next re-entry classification.
  CastPlayer()->NoteLocomotionReentryTick(
      CastPlayer()->IsEligibleForProceduralLocomotion(ball_retainer == player),
      CastPlayer()->IsLocomotionIntentRefreshDue(
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
      tick.designated_possession_player == player;
  const bool designated_team_possession_player =
      tick.own_team.DesignatedPlayer() == player;
  const auto player_decision_cadence = PlayerDecisionCadenceForContext(
      designated_possession_player, designated_team_possession_player,
      decision_distance_to_ball);
  const bool continuity_repair_due =
      CastPlayer()->IsEligibleForProceduralLocomotion(ball_retainer == player) &&
      CastPlayer()->DecisionLocomotionEpochIsStale();
  const bool player_decision_due =
      CastPlayer()->IsPlayerDecisionRefreshDue(
          decision_now, player_decision_cadence);
  if (continuity_repair_due || player_decision_due) {
    PlayerCommandQueue player_decision_commands;
    CastPlayer()->RequestCommand(player_decision_commands,
        {tick.ball, tick.touches, tick.restart,
         ball_retainer, tick.pitch, tick.own_team});
    CastPlayer()->PublishPlayerDecisionQueue(
        player_decision_commands, decision_now);
    bool has_movement = false;
    for (const PlayerCommand &command : player_decision_commands)
      has_movement |= command.desiredFunctionType == e_FunctionType_Movement &&
                      command.useDesiredMovement;
    CastPlayer()->NoteControllerQuery(has_movement, decision_now, ball_retainer == player);
    RecordPlayerDecisionQuery(
        CastPlayer(), player_decision_commands, decision_now_ms,
        continuity_repair_due ? PlayerDecisionQueryCause::ContinuityRepair
                              : PlayerDecisionQueryCause::PlayerDecisionClockPeriodic,
        CastPlayer()->GetSimulationActionState().type);
    ++PlayerDecisionClockQueries();
    if (continuity_repair_due) ++PlayerDecisionClockForcedQueries();
    else ++PlayerDecisionClockPeriodicQueries();
  }

  // Continuity repair forces the independent Player Decision Clock, then
  // publishes its fresh Movement axis before locomotion execution. It does not
  // ask the controller through the legacy locomotion/animation queue.
  if (continuity_repair_due) {
    ++ContinuityRepairAttempts();
    CastPlayer()->NoteDecisionPublicationCause(2);
    if (CastPlayer()->HasPlayerDecisionQueue() &&
        CastPlayer()->PublishMovementIntentFromQueue(
            CastPlayer()->GetPlayerDecisionQueue(), decision_now, ball_retainer == player)) {
      movement_published_this_tick = true;
      CastPlayer()->CommitLocomotionIntentRefresh(tick.ball, decision_now, ball_retainer == player);
      ++ContinuityRepairPublications();
    } else {
      ++ContinuityRepairCandidatesMissing();
    }
    CastPlayer()->NoteDecisionPublicationCause(0);
  }

  CalculateSpatialState(ball_retainer);
  spatialState.positionOffsetMovement = Vector3(0);
  // H3e1c-3c: pure locomotion ticks are produced by the simulation; every
  // other tick keeps the legacy root motion. Either way the legacy movement
  // fields end up as a projection of the authoritative kinematic state.
  ProjectMovementState(tickStartState, ball_retainer, tick.restart_needs_simulation);

  CastPlayer()->AdvanceActionTick();

  const PlayerActionState &action =
      CastPlayer()->GetSimulationActionState();
  previousAnim_frameNum++;


  /*

    bump if (currentAnim.positionOffset.GetLength() > 0.1f && interruptAnim ==
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
    (currentAnim.originatingInterrupt != e_Interrupt_None &&
          currentAnim.originatingInterrupt != e_Interrupt_Switch) {
     mayRequeue = false;
    }*/

  if (mayReQueue) {
    bool frameNumPredicate = false;
    float actionDistance = ((spatialState.position + spatialState.movement * 0.1f) - tick.ball.Predict(football::sim::TickSpan{10}).position.Get2D()).GetLength();

    int team_id = tick.processing_slot;
    using namespace football::sim::player_timing;
    const auto phase = static_cast<std::uint64_t>(team_id);
    if (tick.designated_possession_player == player &&
        actionDistance < 3.0f) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwnerNear, football::sim::TickSpan{phase});

    } else if (tick.designated_possession_player == player) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwner, football::sim::TickSpan{phase});

    } else if (tick.own_team.DesignatedPlayer() == player) {
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

    float ballDistance = (currentMentalImage->GetBallPrediction(500, now, tick.ball).Get2D() - spatialState.position).GetLength();
    if (((action.type == e_FunctionType_Movement &&
            !CastPlayer()->HasPossession() && ballDistance < 16.0f) ||
           (action.type == e_FunctionType_Movement &&
            CastPlayer()->HasPossession()) ||  // passes / shot
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
      CastPlayer()->NoteLocomotionIntentCadence(legacy_opportunity, decision_now, ball_retainer == player);
  // Tag the cause of any publication on this path, so a legacy-driven query is
  // not credited to the simulation cadence in the audit.
  const bool legacy_only = legacy_opportunity && !simulation_due;
  CastPlayer()->NoteDecisionPublicationCause(legacy_only ? 1 : 0);
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
      CastPlayer()->BuildTripSelectionCommands(
          commandQueue, tripDirection, tripType, spatialState.floatVelocity,
          spatialState.directionVec, spatialState.position);
      // All but the last command are trips; the last is the movement fallback.
      while (commandProvenance.size() + 1 < commandQueue.size()) {
        commandProvenance.push_back(PlayerPathSelectionCommandProvenance::LocalTrip);
      }
      tripType = 0;
      commandProvenance.push_back(
          PlayerPathSelectionCommandProvenance::LocalTripMovementFallback);
    } else {
      if (!CastPlayer()->HasPlayerDecisionQueue()) {
        ++PlayerDecisionClockQueueConsumersMissing();
        throw std::logic_error(
            "Player Decision queue missing before animation selection");
      }
      commandQueue = CastPlayer()->GetPlayerDecisionQueue();
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
      found = CastPlayer()->TrySelectAction(now, tick, command, history, interruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
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
        logger.warning("Humanoid::Process: no applicable animation; current type {}",
                       GetCurrentBakedClip().metadata.action_type);
        for (const auto& command : commandQueue) {
          logger.warning("desired type {}, velocity {}, direction ({}, {}, {})",
                         command.desiredFunctionType, command.desiredVelocityFloat,
                         command.desiredDirection.coords[0],
                         command.desiredDirection.coords[1],
                         command.desiredDirection.coords[2]);
        }
        logger.warning("current velocity {}, body angle {}, relative angle {}, state {}",
                       spatialState.floatVelocity, static_cast<float>(spatialState.bodyAngle),
                       static_cast<float>(spatialState.relBodyAngle),
                       GetCurrentBakedClip().metadata.outgoing_special_state);
      }
      throw std::runtime_error("Humanoid::Process: no applicable animation");
    }


    if (uses_player_decision_queue) {
      decision_queue_selection_movement =
          found && action.type == e_FunctionType_Movement;
    }
    if (found) {
      startPos = spatialState.position;
      startAngle = spatialState.angle;

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
    if (!CastPlayer()->HasPlayerDecisionQueue()) {
      ++PlayerDecisionClockQueueConsumersMissing();
      throw std::logic_error(
          "Player Decision queue missing before locomotion publication");
    }
    CastPlayer()->NoteDecisionMovementSelection(
        decision_queue_selection_movement);
    if (CastPlayer()->PublishMovementIntentFromQueue(
            CastPlayer()->GetPlayerDecisionQueue(), decision_now, ball_retainer == player)) {
      movement_published_this_tick = true;
      ++PlayerPathDirectPublications();
      CastPlayer()->CommitLocomotionIntentRefresh(tick.ball, decision_now, ball_retainer == player);
      ++PlayerPathRefreshCommits();
    } else {
      ++PlayerPathCandidatesMissing();
    }
  }
  reQueueDelayFrames = std::max(reQueueDelayFrames - 1, 0);

  interruptAnim = e_InterruptAnim_None;

  float ballDistanceNow = (tick.ball.state().position.Get2D() - spatialState.position).GetLength();
  float ballDistanceFuture = (tick.ball.Predict(football::sim::TickSpan{20}).position.Get2D() - (spatialState.position + spatialState.movement * 0.2f)).GetLength();
  float lastTouchBias = CastPlayer()->GetLastTouchBias(1500, now);
  float oppLastTouchBias = football::sim::event::TeamTouchBias(tick.touches,
      tick.opponent_team.team_id, tick.opponent_team.teammates, 240, now);

  if (CastPlayer() == tick.designated_possession_player &&
      ((lastTouchBias <= 0.01f && oppLastTouchBias <= 0.01f &&
        action.type == e_FunctionType_Movement &&
        ballDistanceNow < 0.6f && ballDistanceFuture > 0.65f &&
        ballDistanceFuture > ballDistanceNow)  // 0.5 / 0.6

       ||

       (lastTouchBias <= 0.7f && CastPlayer()->HasPossession() &&
        action.type == e_FunctionType_Trip &&
        ballDistanceNow < 0.4f)

       ) &&
      tick.ball.state().position.coords[2] < 1.6f) {

    CastPlayer()->TriggerControlledBallCollision();
    //SetGreenDebugPilon(spatialState.position);
  }

  // ------------------------ EXPERIMENTAL ------------------------------------------------
  bool controlledBallCollision = CastPlayer()->IsControlledBallCollisionTriggered();
  if (controlledBallCollision) CastPlayer()->ResetControlledBallCollisionTrigger();
  const bool may_touch = tick.ball_in_play ||
      (tick.play_authorized && tick.set_piece_active && tick.restart.active &&
       tick.restart.taker == player);
  if (may_touch && controlledBallCollision && !action.HasScheduledContact()) {
    begin_active_observation(ActiveTouchOrigin::Controlled);
    if (active_observation) active_observation->candidate.reachable = true;
    Vector3 currentBallVec = tick.ball.state().velocity;
    radian nextBodyAngle = startAngle + GetCurrentBakedClip().metadata.outgoing_angle + GetCurrentBakedClip().metadata.outgoing_body_angle + currentAnim.rotationSmuggle.end;

    radian xRot = 0;
    radian yRot = 0;
    Vector3 touchVec = GetTrapVector(&tick.ball, CastPlayer(), tick.touches, tick.opponent_team, now, rng_, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
    if (currentAnim.originatingCommand.modifier &
        e_PlayerCommandModifier_KnockOn) {
      touchVec *= 1.35f;//1.2f;
    }

    float bumpyRideBias = 0.0f;
    touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

    apply_touch(touchVec);
    set_rotation(xRot, yRot, 0, 0.2f * (1.0f - bumpyRideBias)); // 0.9
    notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart)); //, e_TouchType_Accidental
  }
  // ---------------------- / EXPERIMENTAL ------------------------------------------------

  if (action.HasScheduledContact() && action.Frame() <= action.ContactFrame()) {
    begin_active_observation(ActiveTouchOrigin::Scheduled);
    if (active_observation) {
      active_observation->candidate.scheduled_tick = now + football::sim::TickSpan{
          static_cast<std::uint64_t>(action.ContactFrame() - action.Frame() + 1)};
      active_observation->stage = action.Frame() < action.ContactFrame() ?
          ActiveTouchStage::Pending : ActiveTouchStage::NoImpulse;
      if (!may_touch && action.Frame() == action.ContactFrame()) {
        active_observation->stage = ActiveTouchStage::Rejected;
        active_observation->rejection = ActiveTouchReject::Authorization;
      }
    }
  }
  if (may_touch && action.HasScheduledContact() &&
      action.Frame() == action.ContactFrame()) {
    ContactAuthorityAudit *contact_audit =
        ContactAuthorityAuditEnabled() && IsTrackedScheduledContact(action.type)
        ? &ContactAuthorityFor(action.type) : nullptr;
    bool contact_impulse_generated = false;
    if (contact_audit) ++contact_audit->at_contact_frame;
    if (contact_audit) {
      contact_audit->observed_elapsed_ms.push_back(static_cast<int>(football::sim::ToMilliseconds(action.elapsed)));
      if (currentAnim.positionOffset.GetLength() > 0.0f)
        ++contact_audit->contact_position_offset_nonzero;
      if (!currentAnim.positions.empty())
        ++contact_audit->animation_positions_present;
    }

    Vector3 desiredBallPosition;
    for (const auto& t : GetCurrentBakedClip().touches) {
      if (t.frame == action.ContactFrame()) desiredBallPosition = t.position;
    }
    float desiredBallHeight = desiredBallPosition.coords[2];
    if (contact_audit)
      contact_audit->desired_ball_heights.push_back(desiredBallHeight);

    float touchableDistance = 0.4f;

    float fullBallDistance = (tick.ball.state().position - (currentAnim.touchPos + currentAnim.positionOffset)).GetLength();

    if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty()) {
      fullBallDistance = 0.0f;
      touchableDistance = 1.0f;
    }

    float bumpyRideBias = fullBallDistance / touchableDistance;
    bumpyRideBias = clamp(bumpyRideBias - 0.001f, 0.0f, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 0.5f);
    Vector3 currentBallVec = tick.ball.state().velocity;
    bool contact_reachable = false;
    if (active_observation) {
      active_observation->legacy_reach_evaluated = true;
      active_observation->candidate.reach_error = fullBallDistance;
      active_observation->incoming_retain_override = !GetCurrentBakedClip().metadata.incoming_retain_state.empty();
      active_observation->candidate.reachable = fullBallDistance < touchableDistance &&
          std::fabs(desiredBallHeight - tick.ball.state().position.coords[2]) < 1.0f;
      if (!active_observation->candidate.reachable) {
        active_observation->stage = ActiveTouchStage::Rejected;
        active_observation->rejection = fullBallDistance < touchableDistance ?
            ActiveTouchReject::Height : ActiveTouchReject::Distance;
      }
    }
    if (contact_audit) {
      contact_audit->full_ball_distances.push_back(fullBallDistance);
      contact_audit->bumpy_ride_biases.push_back(bumpyRideBias);
      const float height_gap = std::fabs(
          desiredBallHeight - tick.ball.state().position.coords[2]);
      if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty())
        ++contact_audit->incoming_retain_override;
      if (!(fullBallDistance < touchableDistance)) {
        ++contact_audit->distance_rejected;
      } else if (!(height_gap < 1.0f)) {
        ++contact_audit->height_rejected;
      } else {
        ++contact_audit->physically_reachable;
        contact_reachable = true;
      }
    }
    const auto record_contact_impulse = [&](const Vector3 &impulse) {
      if (!contact_audit) return;
      contact_impulse_generated = true;
      ++contact_audit->impulse_calls;
      const float speed = impulse.GetLength();
      contact_audit->impulse_request_speeds.push_back(speed);
      if (speed > 0.0f) ++contact_audit->nonzero_impulse_requests;
    };

    if (fullBallDistance < touchableDistance &&
        std::fabs(desiredBallHeight - tick.ball.state().position.coords[2]) <
            1.0f) {

      radian nextBodyAngle = startAngle + GetCurrentBakedClip().metadata.outgoing_angle + GetCurrentBakedClip().metadata.outgoing_body_angle + currentAnim.rotationSmuggle.end;

      if (currentAnim.functionType == e_FunctionType_Trap ||
          (currentAnim.functionType == e_FunctionType_BallControl &&
           CastPlayer()->HasPossession() == false)) {
        //printf("trap!\n");
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(&tick.ball, CastPlayer(), tick.touches, tick.opponent_team, now, rng_, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        if (currentAnim.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(currentAnim.functionType, touchVec);
        if (!physical) {
          apply_touch(touchVec);
          set_rotation(xRot, yRot, 0, 0.5f * (1.0f - bumpyRideBias));
        }
        record_contact_impulse(touchVec);

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_BallControl) {
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetBallControlVector(&tick.ball, CastPlayer(), tick.opponent_team, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        if (currentAnim.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);
        record_contact_impulse(touchVec);
        set_rotation(xRot, yRot, 0, 0.6f * (1.0f - bumpyRideBias)); // 1.0

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_ShortPass ||
               currentAnim.functionType == e_FunctionType_LongPass ||
               currentAnim.functionType == e_FunctionType_HighPass) {

        Vector3 ballDirection = currentAnim.originatingCommand.touchInfo.desiredDirection;
        float ballPower = currentAnim.originatingCommand.touchInfo.desiredPower;
        Player *targetPlayer = currentAnim.originatingCommand.touchInfo.targetPlayer;
        Vector3 inputDirection = currentAnim.originatingCommand.touchInfo.inputDirection;

        // refine/change target, if new target is close enough to old target

        //targetPlayer = 0;//currentAnim.originatingCommand.touchInfo.targetPlayer;
        Vector3 tmpBallDirection = ballDirection;
        float tmpBallPower = ballPower;
        Player *tmpTargetPlayer = 0;
        Player *forcedTargetPlayer = 0;
        football::sim::mechanics::GetPass(CastPlayer(), tick.own_team.teammates, currentAnim.originatingCommand.desiredFunctionType, inputDirection, currentAnim.originatingCommand.touchInfo.inputPower, currentAnim.originatingCommand.touchInfo.autoDirectionBias, currentAnim.originatingCommand.touchInfo.autoPowerBias, tmpBallDirection, tmpBallPower, tmpTargetPlayer, currentAnim.originatingCommand.touchInfo.forcedTargetPlayer);
        float maxDeviationAngle = 0.15f * pi;
        radian angleDiff = tmpBallDirection.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) <= maxDeviationAngle) {
          ballDirection = tmpBallDirection;
          ballPower = tmpBallPower;
          targetPlayer = tmpTargetPlayer;
        } else if (std::fabs(angleDiff) < 2.0f * maxDeviationAngle) {
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);

          if (tmpTargetPlayer != targetPlayer) {
            // if we can't make it to our refined target at all, just stick with original ballpower (think about refined target at ~180 deg, would be weird to pass forward with the power of that (unreachable) target)
            float refinedBias = NormalizedClamp(std::fabs(clampedAngleDiff), 0.0f, std::fabs(angleDiff));
            ballPower = ballPower * (1.0f - refinedBias) + tmpBallPower * refinedBias;
            targetPlayer = tmpTargetPlayer; // new, and removed line below
          } else {
            ballPower = tmpBallPower;
          }

        }  // else: just stick to original

        if (targetPlayer) {
          tick.own_team.SetDesignatedPlayer(targetPlayer);

        }
        float zcurve = 0.0f;
        Vector3 touchVec = ballDirection * 36 * (ballPower + 0.3f);

        if (_PassFiddlingEnabled()) {
          if (contact_audit) ++contact_audit->pass_fiddling;
          //SetGreenDebugPilon(tick.ball.state().position.Get2D() + touchVec.Get2D() * 0.4f);

          touchVec = GetBestPossibleTouch(now, tick, touchVec, currentAnim.functionType);

          // add a little curve for aesthetics & realism
          radian bodyTouchAngle = spatialState.bodyDirectionVec.GetAngle2D(touchVec) / pi;
          if (std::fabs(bodyTouchAngle) > 0.5f) bodyTouchAngle = (1.0f - std::fabs(bodyTouchAngle)) * signSide(bodyTouchAngle);
          bodyTouchAngle *= 2.0f;
          //printf("bodyTouchAngle: %f\n", bodyTouchAngle);
          radian amount = bodyTouchAngle * 0.25f;
          if (currentAnim.functionType == e_FunctionType_HighPass) amount *= 0.2f;
          touchVec.Rotate2D(amount * (0.4f + 0.6f * NormalizedClamp(touchVec.GetLength(), 0.0f, 70.0f)));
          zcurve = amount * -340;//-600;

          //SetRedDebugPilon(tick.ball.state().position.Get2D() + touchVec.Get2D() * 0.4f);
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(currentAnim.functionType, touchVec);
        if (!physical) apply_touch(touchVec);
        record_contact_impulse(touchVec);
        float forwardness = 3.5f;
        if (currentAnim.functionType == e_FunctionType_HighPass) forwardness = -1.3f;
        radian xRot = touchVec.GetNormalized(0).coords[1] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        radian yRot = touchVec.GetNormalized(0).coords[0] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        if (!physical) set_rotation(xRot, yRot, zcurve, 0.9f * (1.0f - bumpyRideBias));

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_Shot) {

        // alter direction, if needed
        Vector3 ballDirection = currentAnim.originatingCommand.touchInfo.desiredDirection;
        Vector3 inputDirection = currentAnim.originatingCommand.touchInfo.inputDirection;
        Vector3 ballDirectionAltered = football::sim::mechanics::GetShotDirection(CastPlayer(), tick.pitch, inputDirection, currentAnim.originatingCommand.touchInfo.autoDirectionBias);

        float maxDeviationAngle = 0.1f * pi;
        radian angleDiff = ballDirectionAltered.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) > maxDeviationAngle) {
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);
        } else {
          ballDirection = ballDirectionAltered;
        }

        radian xRot = 0;
        radian yRot = 0;
        radian zRot = 0;
        Vector3 touchVec = GetShotVector(&tick.ball, CastPlayer(), GetCurrentBakedClip(), rng_, currentAnim, spatialState, decayingPositionOffset, xRot, yRot, zRot);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        const bool physical = physical_touch(currentAnim.functionType, touchVec);
        if (!physical) {
          apply_touch(touchVec);
          set_rotation(xRot, yRot, zRot, 0.7f * (1.0f - bumpyRideBias));
        }
        record_contact_impulse(touchVec);
        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_Interfere) {
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(&tick.ball, CastPlayer(), tick.touches, tick.opponent_team, now, rng_, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        touchVec =
            touchVec * 0.5f +
            (tick.ball.state().position.Get2D() - spatialState.position)
                    .GetNormalized() *
                4.0f +
            Vector3(0, 0, rng_.Uniform(0.5f, 1.5f));  // was 1 .. 6

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);
        // Legacy three-argument call: the third value is z rotation; bias was 1.0.
        set_rotation(xRot, yRot, 0.3f * (1.0f - bumpyRideBias), 1.0f);
        notify_touch(e_TouchType_Accidental); // it's not truly accidental, but the resulting direction somewhat is, so goalies may fetch these balls
      }

      else if (currentAnim.functionType == e_FunctionType_Deflect) {
        bool canRetain = true; // can we grab hold of the ball?
        if (GetCurrentBakedClip().metadata.outgoing_retain_state.compare("") == 0) canRetain = false; // not the right anim, hopeless!
        if (ball_retainer != 0) canRetain = false; // somebody is already holding the ball :( (dafuq, this should not happen, right?)

        float veloDifficulty = NormalizedClamp((tick.ball.state().velocity - spatialState.movement).GetLength(), 0.0f, 40.0f);
        float reactionDifficulty = 0.0f;
        Player *lastTouchPlayer = football::sim::event::LastTouchPlayer(tick.touches,
            tick.opponent_team.team_id, tick.opponent_team.teammates);
        if (lastTouchPlayer) {
          reactionDifficulty =
              std::pow(lastTouchPlayer->GetLastTouchBias(
                           1200 - player->GetStat(football::player::PlayerStat::physical_reaction) * 400, now),
                       0.6f);
        }
        if ((1.0f - veloDifficulty) * (1.0f - reactionDifficulty) < 0.3f) canRetain = false; // too hard!

        if (canRetain) {
          if (active_observation) {
            active_observation->origin = ActiveTouchOrigin::RetainAcquire;
            active_observation->stage = ActiveTouchStage::Constraint;
          }
          runtime_sink.SetBallRetainer(CastPlayer());
          ball_retainer = CastPlayer();
        } else {
          Vector3 currentBallMovement = tick.ball.state().velocity.Get2D();
          Vector3 playerMovement = spatialState.movement;
          Vector3 touchVec =
              (-currentBallMovement * 0.1f + playerMovement * 2.0f +
               Vector3(-CastPlayer()->GetDynamicSide(), 0, 0) * 4.0f +
               Vector3(0, rng_.Uniform(-1, 1), 0))
                  .GetNormalized(0) *
              (currentBallMovement.GetLength() * 0.3f +
               playerMovement.GetLength() * 2.5f);
          touchVec.coords[2] += 1.2f;

          touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

          apply_touch(touchVec);
          set_rotation(0, 0, 0, 0.2f * (1.0f - bumpyRideBias));
        }
        notify_touch(e_TouchType_Accidental);
      }

      else if (currentAnim.functionType == e_FunctionType_Sliding) {
        Vector3 touchVec = GetCurrentBakedClip().metadata.outgoing_ball_direction.GetRotated2D(spatialState.angle);
        touchVec = touchVec * 6.0f + tick.ball.state().velocity * -0.28f;
        touchVec += Vector3(0, 0, 6);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        apply_touch(touchVec);

        notify_touch(e_TouchType_Accidental);
      }
    }
    if (contact_audit && !contact_impulse_generated) {
      ++contact_audit->suppressed;
      if (contact_reachable) ++contact_audit->reachable_without_impulse;
    }
  }
  flush_active_observation();

  if (ball_retainer == player) {
    if (((!action.HasScheduledContact() ||
          action.Frame() >= action.ContactFrame()) &&
         GetCurrentBakedClip().metadata.outgoing_retain_state != "") ||
        (action.HasScheduledContact() &&
         action.Frame() < action.ContactFrame() &&
         GetCurrentBakedClip().metadata.incoming_retain_state != "") ||
        (GetCurrentBakedClip().metadata.incoming_retain_state != "" &&
         GetCurrentBakedClip().metadata.outgoing_retain_state != "")) {
      // Anchor the ball to a body-semantic local offset. The retain state
      // string decides which anchor; the position itself is a pure function of
      // simulation state, so it no longer depends on when the animation pose
      // was last refreshed.
      std::string retainState =
          GetCurrentBakedClip().metadata.outgoing_retain_state;
      if (retainState.empty()) {
        retainState = GetCurrentBakedClip().metadata.incoming_retain_state;
      }
      RetainAnchorKind anchor;
      if (!ParseRetainAnchorKind(retainState, anchor)) {
        // Fail fast: a silent fallback would place the retained ball
        // somewhere plausible but wrong.
        throw std::runtime_error("Humanoid::Process: unknown retain state: " +
                                 retainState);
      }
      begin_active_observation(ActiveTouchOrigin::RetainAnchor);
      apply_touch(Vector3(0));
      if (active_observation) active_observation->stage = ActiveTouchStage::Constraint;
      set_rotation(0, 0, 0, 1.0);
      const auto anchor_position = ComputeRetainAnchor(
          spatialState.position, spatialState.bodyDirectionVec, anchor);
      prepared_anchor = anchor_position;
      notify_touch(e_TouchType_Intentional_Nonkicked);
    } else {
      // no longer retaining
      begin_active_observation(ActiveTouchOrigin::RetainRelease);
      if (active_observation) active_observation->stage = ActiveTouchStage::Constraint;
      flush_active_observation();
      runtime_sink.SetBallRetainer(nullptr);
      ball_retainer = nullptr;
    }
  }

  // ANIM-1 A9 step 2: the action/movement/rotation smuggle evolution is a pure
  // PlayerMotion calculation; this applies its deltas in the legacy order.
  ActionSmuggleTickInput smuggle_input;
  smuggle_input.frame_num = currentAnim.frameNum;
  smuggle_input.touch_frame = currentAnim.touchFrame;
  smuggle_input.frame_count = static_cast<int>(GetCurrentBakedClip().frame_count);
  smuggle_input.action_smuggle = currentAnim.actionSmuggle;
  smuggle_input.movement_smuggle = currentAnim.movementSmuggle;
  smuggle_input.rotation_smuggle_begin = currentAnim.rotationSmuggle.begin;
  smuggle_input.rotation_smuggle_end = currentAnim.rotationSmuggle.end;
  smuggle_input.allow_pre_touch_rotation_smuggle = allowPreTouchRotationSmuggle;
  const ActionSmuggleTickResult smuggle =
      PlayerMotion::ActionSmuggleTick(smuggle_input);

  spatialState.actionSmuggleMovement = smuggle.action_smuggle_movement;
  currentAnim.actionSmuggleOffset += smuggle.action_smuggle_offset_delta;
  spatialState.movementSmuggleMovement = smuggle.movement_smuggle_movement;
  currentAnim.movementSmuggleOffset += smuggle.movement_smuggle_offset_delta;
  currentAnim.rotationSmuggleOffset = smuggle.rotation_smuggle_offset;

  // ANIM-1 A9 step 3: the goalkeeper boundary clamps are Player-owned.
  CastPlayer()->ApplyGoalkeeperBoundaryClamps(tick, ball_retainer);

  // next frame

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    //printf("size: %i\n", currentAnim.positions.size());
  } else {
  }

  // ANIM-1 A5: the Player owns the final pose. The Humanoid only hands over the
  // selected clip/frame and the base yaw once per tick; the Player's playback
  // state samples/blends it.
  // ANIM-1 A6.3c.3: the clip id and frame come from the Player authority.
  CastPlayer()->UpdatePlaybackFromAuthority(
      startAngle + currentAnim.rotationSmuggleOffset,
      BlendTicksForSwitch(currentAnim.functionType == e_FunctionType_Movement &&
                          CastPlayer()->IsEligibleForProceduralLocomotion(ball_retainer == player)));
}


void Humanoid::ResetSituation(const Vector3 &focusPos) {
  HumanoidBase::ResetSituation(focusPos);
  //printf("humanoid reset\n");
}


// ANIM-1 A6.4 step 7a: the compat install projection. It writes the fields of
// the legacy `currentAnim` from a selection result and deliberately does NOT
// install; `Player::CommitSelectedAction` owns the dispatch.
void Humanoid::ProjectSelectedActionCompat(
    int selectedAnimID, const football::sim::TouchChoice &touch_choice,
    const PlayerCommand &command, const football::sim::PlayerTickContext &tick,
    football::sim::Tick now, std::span<MentalImage> history,
    e_InterruptAnim localInterruptAnim, bool material_candidate) {
  const PlayerActionState &action = CastPlayer()->GetSimulationActionState();
  previousAnim_frameNum = currentAnim.frameNum;
  previousAnim_functionType = currentAnim.functionType;

  currentAnim.animationId = selectedAnimID;
  currentAnim.functionType = command.desiredFunctionType;
  currentAnim.frameNum = 0;
  currentAnim.touchFrame = touch_choice.touch_frame;
  currentAnim.originatingInterrupt = localInterruptAnim;
  currentAnim.touchPos = touch_choice.touch_position;
  currentAnim.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, spatialState.relBodyAngleNonquantized - GetCurrentBakedClip().metadata.incoming_body_angle) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
  currentAnim.rotationSmuggle.end = touch_choice.rotation_smuggle;
  currentAnim.rotationSmuggleOffset = 0;
  currentAnim.actionSmuggle = touch_choice.action_smuggle;
  currentAnim.actionSmuggleOffset = Vector3(0);
  currentAnim.actionSmuggleSustain = Vector3(0); // calculated below
  currentAnim.actionSmuggleSustainOffset = Vector3(0);
  currentAnim.movementSmuggle = Vector3(0); // needs to be reset here, else the previous calc is used in upcoming 'calculatemovementsmuggle'
  currentAnim.movementSmuggleOffset = Vector3(0);
  currentAnim.incomingMovement = spatialState.movement;
  currentAnim.outgoingMovement = CalculateOutgoingMovement(touch_choice.positions);
  currentAnim.positions.clear();
  currentAnim.positions.assign(touch_choice.positions.begin(), touch_choice.positions.end());
  currentAnim.positionOffset = Vector3(0);
  currentAnim.originatingCommand = command;
  RecordMovementCommandAcceptance(material_candidate, action.type,
                                  static_cast<int>(football::sim::ToMilliseconds(action.elapsed)), localInterruptAnim,
                                  command, static_cast<int>(GetCurrentBakedClip().frame_count));
  // Must run after the currentAnim field writes above (it reads the new
  // animationId/positions/touchFrame and GetCurrentBakedClip), before install.
  currentAnim.movementSmuggle = CastPlayer()->ComputeMovementSmuggle(now, tick, command.desiredDirection, command.desiredVelocityFloat, history);
  currentAnim.movementSmuggleOffset = Vector3(0);
}

void Humanoid::RecordSelectedContactAudit(const PlayerCommand &command) {
  const PlayerActionState &scheduled = CastPlayer()->GetSimulationActionState();
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

// ANIM-1 A6.4 step 5: the constraint half of SelectAnim, extracted so the
// Player can own it. It builds the crude `CrudeSelectionQuery` from the
// command, the tick context and the spatial state only.
// `CrudeSelectionQuery query;` is the first statement of the original block.




Vector3 Humanoid::GetBestPossibleTouch(football::sim::Tick now, const football::sim::PlayerTickContext& tick, const Vector3 &desiredTouch,
                                       e_FunctionType functionType) {
  constexpr float maxPowerShortPass = 30.0f;
  constexpr float maxPowerHighPass  = 42.0f;
  float maxPowerBase = maxPowerShortPass;
  if (functionType == e_FunctionType_HighPass) maxPowerBase = maxPowerHighPass;

  Vector3 resultTouch = desiredTouch;

  // fetch vars

  float maxPowerFactor = GetCurrentBakedClip().metadata.touch_max_power_factor;
  if (maxPowerFactor == 0.0f) maxPowerFactor = 1.0f;
  maxPowerFactor = maxPowerFactor * 0.7f + 0.3f;

  // clamp to maximum possible power (from anim vars)

  float maxPower = maxPowerBase * maxPowerFactor * (1.0f - clamp(decayingPositionOffset.GetLength() * 2.5f, 0.0f, 0.25f));
  maxPower += tick.ball.state().velocity.GetLength() * 0.5f; // can use some of current ballmomentum
  if (resultTouch.GetLength() > maxPower) {
    float missingPower = resultTouch.GetLength() - maxPower;
    resultTouch = resultTouch.GetNormalized(0) * maxPower;
    resultTouch.coords[2] += clamp(missingPower, 0.0f, 10.0f) * 0.25f;
  }

  // difficulty

  float difficultyFactor = GetCurrentBakedClip().metadata.touch_difficulty_factor;

  // apply stats
  if (functionType == e_FunctionType_ShortPass ||
      functionType == e_FunctionType_LongPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(football::player::PlayerStat::technical_shortpass) * 0.5f);
  if (functionType == e_FunctionType_HighPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(football::player::PlayerStat::technical_highpass)  * 0.5f);

  float distanceFactor = 0.0f;
  float heightFactor = 0.0f;
  float ballMovementFactor = 0.0f;
  GetDifficultyFactors(&tick.ball, CastPlayer(), tick.touches, tick.opponent_team, now, rng_, spatialState, decayingPositionOffset, distanceFactor, heightFactor, ballMovementFactor);

  // difficult balls may go into a more random orientation, or, if the anim has a default outgoing direction, it may converge towards that (since it is the easiest direction for that anim)
  radian randomRotation = 0.0f;
  randomRotation = distanceFactor * 0.15f + heightFactor * 0.15f + ballMovementFactor * 0.3f + difficultyFactor * 0.5f;
  Vector3 animBallDirection = GetCurrentBakedClip().metadata.outgoing_ball_direction.GetRotated2D(startAngle + currentAnim.rotationSmuggleOffset);
  if (animBallDirection.GetLength() > 0.01f) {
    float bias = clamp(randomRotation * 1.5f, 0.0f, 1.0f);
    Vector3 nativeTouch = animBallDirection.GetNormalized(resultTouch).Get2D() * resultTouch.GetLength() + resultTouch * Vector3(0, 0, 1);
    resultTouch = resultTouch * (1.0f - bias) + nativeTouch * bias;
  } else {
    radian rotation =
        rng_.Uniform(-0.5f * pi, 0.5f * pi) * std::min((real) randomRotation, 0.5f);
    resultTouch.Rotate2D(rotation);
  }

  // ball far away == less power
  resultTouch *= 1.0f - distanceFactor * 0.3f;
  resultTouch.coords[2] += distanceFactor * 1.5f; // try to correct (add power) by playing higher ball (== less ground friction)

  resultTouch.coords[2] += tick.ball.state().velocity.coords[2] * heightFactor * 0.5f +
                           heightFactor * 1.0f;

  resultTouch = resultTouch * (1.0f - ballMovementFactor) +
                tick.ball.state().velocity * ballMovementFactor;

  resultTouch.coords[2] += difficultyFactor * 5.0f * rng_.Uniform(0.2f, 1.0f);

  return resultTouch;
}


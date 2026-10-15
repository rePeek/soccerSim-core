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
constexpr bool enableMovementSmuggle = true;
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle = animSmoothing ? 0.25f * pi : 0.0f;
constexpr int initialReQueueDelayFrames = 22;
constexpr int minRemainingMovementReQueueFrames = 6; // after this # of frames remaining, just let anim finish; we're almost there anyway
constexpr int minRemainingTrapReQueueFrames = 6; // after this # of frames remaining to touchframe, just let anim finish; we're almost there anyway
constexpr int maxBallControlReQueueFrame = 8;
constexpr bool allowReQueue = true;
constexpr bool allowMovementReQueue = true;
constexpr bool allowBallControlReQueue = true;
constexpr bool allowTrapReQueue = true;
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
      AddTripCommandToQueue(commandQueue, tripDirection, tripType);
      while (commandProvenance.size() < commandQueue.size()) {
        commandProvenance.push_back(PlayerPathSelectionCommandProvenance::LocalTrip);
      }
      tripType = 0;
      commandQueue.push_back(GetBasicMovementCommand(tripDirection, spatialState.floatVelocity)); // backup, if there's no applicable trip anim
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
      found = SelectAnim(now, tick, command, history, interruptAnim, preferPassAndShot);
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

  // action smuggle

  // start with +1, because we want to influence the first frame as well
  // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
  // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
  float frameBias = (currentAnim.frameNum + 1) / (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) + 1);

  if (currentAnim.touchFrame != -1 &&
      currentAnim.frameNum <= currentAnim.touchFrame) {
    // linear version *outdated*
    // spatialState.actionSmuggleMovement = currentAnim.actionSmuggle / (float)(currentAnim.touchFrame - 1.0f);
    // currentAnim.actionSmuggleOffset += spatialState.actionSmuggleMovement;

    // smooth version
    assert(currentAnim.touchFrame > 0.0f);
    float value =
        std::cos((currentAnim.frameNum / (float)(currentAnim.touchFrame + 1) -
                  0.5f) *
                 pi * 2.0f) +
        1.0f;
    // add some linearity
    value = value * 0.1f + 0.9f;
    spatialState.actionSmuggleMovement = (currentAnim.actionSmuggle / (float)(currentAnim.touchFrame + 1)) * value * 100.0f;
    currentAnim.actionSmuggleOffset += spatialState.actionSmuggleMovement / 100.0f;

  } else {
    spatialState.actionSmuggleMovement = Vector3(0);
  }

  // movement smuggle

  if (currentAnim.touchFrame == -1 &&
      currentAnim.frameNum <= (static_cast<int>(GetCurrentBakedClip().frame_count) - 1)) {
      // omit one frame, or balltouch will be influenced because
                    // of velo
    // linear version *outdated*
    // spatialState.movementSmuggleMovement = currentAnim.movementSmuggle / (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1));
    // currentAnim.movementSmuggleOffset += spatialState.movementSmuggleMovement;

    // smooth version
    float value =
        std::cos((currentAnim.frameNum /
                      (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) + 1) -
                  0.5f) *
                 pi * 2.0f) +
        1.0f;
    // add some linearity
    value = value * 0.1f + 0.9f;
    spatialState.movementSmuggleMovement = (currentAnim.movementSmuggle / (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) + 1)) * value * 100.0f;
    currentAnim.movementSmuggleOffset += spatialState.movementSmuggleMovement / 100.0f;
  } else {
    spatialState.movementSmuggleMovement = Vector3(0);
  }

  // rotation smuggle

  int beginRotationFrameCount = 16; // after this amount of frames, be ready with 'ease-in' rotation smuggle
  float cappedFrameBias = std::min(1.0f, (currentAnim.frameNum + 1) / (float)std::min(beginRotationFrameCount, (static_cast<int>(GetCurrentBakedClip().frame_count) - 1) + 1));
  float beginFrameBias = cappedFrameBias;
  float endFrameBias = cappedFrameBias;
  if (currentAnim.touchFrame != -1) {
    // beginFrameBias ranges from 0 to 1 during frame 0 to (touchframe OR beginRotationFrameCount) (depending on which comes first)
    beginFrameBias = std::min(1.0f, (currentAnim.frameNum + 1) / (float)std::min(beginRotationFrameCount, currentAnim.touchFrame + 1));
    if (!allowPreTouchRotationSmuggle) {
      if (currentAnim.frameNum > currentAnim.touchFrame) {
        // end rotation smuggle starts after touch
        endFrameBias = (currentAnim.frameNum - currentAnim.touchFrame) / (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) - currentAnim.touchFrame);
      } else {
        // no smuggle before touch
        endFrameBias = 0.0f;
      }
    }
  }
  currentAnim.rotationSmuggleOffset = currentAnim.rotationSmuggle.begin * (1.0f - beginFrameBias) +
                                       currentAnim.rotationSmuggle.end   * endFrameBias;

  // ballretainer should not get out of 16 meter box

  if (ball_retainer == player &&
      CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK &&
      (tick.set_piece_active == false && tick.play_authorized == true)) {
    if (tick.ball.state().position.coords[1] > 20.05f) {
      CastPlayer()->ApplyPositionCorrection(Vector3(0, clamp(20.05f - tick.ball.state().position.coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (tick.ball.state().position.coords[1] < -20.05f) {
      CastPlayer()->ApplyPositionCorrection(Vector3(0, clamp(-20.05f - tick.ball.state().position.coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (tick.ball.state().position.coords[0] * -CastPlayer()->GetDynamicSide() >
        -tick.pitch.half_length() + 16.4f) {
      CastPlayer()->ApplyPositionCorrection(Vector3(clamp((-tick.pitch.half_length() + 16.4f) -
                                       tick.ball.state().position.coords[0] *
                                           -CastPlayer()->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -CastPlayer()->GetDynamicSide() * 0.3f);
    }
    if (tick.ball.state().position.coords[0] * -CastPlayer()->GetDynamicSide() <
        -tick.pitch.half_length() + 0.1f) {
      CastPlayer()->ApplyPositionCorrection(Vector3(clamp((-tick.pitch.half_length() + 0.1f) -
                                       tick.ball.state().position.coords[0] *
                                           -CastPlayer()->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -CastPlayer()->GetDynamicSide() * 0.4f);
    }
  }

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

void Humanoid::SelectRetainAnim() {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;
  query.properties.incoming_retain_state = "right_elbow";

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);

  assert(dataSet.size() != 0);

  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareMovementSimilarity, this, _1, _2));

  startAngle = FixAngle((Vector3(0) - startPos).GetAngle2D());//0.5 * pi; (facing right)

  currentAnim.positions.clear();
  currentAnim.animationId = *dataSet.begin();
  currentAnim.frameNum = 0;
  currentAnim.touchFrame = -1;
  currentAnim.actionSmuggle = Vector3(0);
  currentAnim.actionSmuggleOffset = Vector3(0);
  currentAnim.actionSmuggleSustain = Vector3(0);
  currentAnim.actionSmuggleSustainOffset = Vector3(0);
  currentAnim.movementSmuggle = Vector3(0);
  currentAnim.movementSmuggleOffset = Vector3(0);
  currentAnim.rotationSmuggle.begin = 0;
  currentAnim.rotationSmuggle.end = 0;
  currentAnim.rotationSmuggleOffset = 0;
  currentAnim.functionType = e_FunctionType_Movement;
  CastPlayer()->InstallSelectedAction(CurrentSelectedAction());


}

void Humanoid::ResetSituation(const Vector3 &focusPos) {
  HumanoidBase::ResetSituation(focusPos);
  //printf("humanoid reset\n");
}

bool Humanoid::SelectAnim(football::sim::Tick now, const football::sim::PlayerTickContext& tick, const PlayerCommand &command,
                          std::span<MentalImage> history,
                          e_InterruptAnim localInterruptAnim,
                          bool preferPassAndShot) {
    // returns false on no applicable anim found
  assert(command.desiredDirection.coords[2] == 0.0f);
  const PlayerActionState &action =
      CastPlayer()->GetSimulationActionState();
  const bool material_candidate =
      RecordSchedulerQuery(currentAnim.originatingCommand, command);

  // optimizations
  auto currentMentalImage = football::sim::observation::SampleMentalImage(history, mentalImageTime);
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding) {
    if ((currentMentalImage->GetBallPrediction(200, now, tick.ball).Get2D() -
         spatialState.position)
            .GetLength() > 10.0f) {
      return false;
    }
    if ((currentMentalImage->GetBallPrediction(defaultTouchOffset_ms, now, tick.ball).Get2D() -
         spatialState.position)
                .GetLength() > 2.0f &&
        //    tick.ball.state().velocity.GetNormalized(0).GetDotProduct(player->GetMovement().GetNormalizedMax(1.0f))
        //    < 0) { // ball and player going the other way
        (currentMentalImage->GetBallPrediction(defaultTouchOffset_ms, now, tick.ball).Get2D() -
         (spatialState.position +
          spatialState.movement * defaultTouchOffset_ms * 0.001))
                .GetLength() >
            (currentMentalImage->GetBallPrediction(0, now, tick.ball).Get2D() -
             (spatialState.position))
                .GetLength()) {
        // ball moving away from player
      return false;
    }
  }

  // this stops these anims from happening when opp has touched the ball, deflecting the ball too far away
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding &&
      command.desiredFunctionType != e_FunctionType_Deflect &&
      tick.ball_retainer != player) {
    if ((currentMentalImage->GetBallPrediction(1000, now, tick.ball) - tick.ball.Predict(football::sim::TickSpan{100}).position).GetLength() > 2.0f) return false;
  }

  // /optimizations

  if (localInterruptAnim == e_InterruptAnim_ReQueue) {
    // ANIM-1 A6.4 step 6a: the pre-search ReQueue gate is a Player-owned pure
    // decision. The Humanoid supplies the not-yet-migrated legacy facts.
    football::sim::RequeueGateContext requeue_gate;
    requeue_gate.action_type = action.type;
    requeue_gate.action_frame = action.Frame();
    requeue_gate.action_frame_count = action.FrameCount();
    requeue_gate.action_contact_frame = action.ContactFrame();
    requeue_gate.originating_command = currentAnim.originatingCommand;
    requeue_gate.command = command;
    requeue_gate.spatial_movement = spatialState.movement;
    requeue_gate.outgoing_movement = currentAnim.outgoingMovement;
    requeue_gate.incoming_movement = currentAnim.incomingMovement;
    requeue_gate.has_possession = CastPlayer()->HasPossession();
    requeue_gate.last_touch_tick = CastPlayer()->GetLastTouchTick();
    requeue_gate.last_touch_type = CastPlayer()->GetLastTouchType();
    requeue_gate.focus_distance =
        (tick.designated_possession_player->GetPosition() - spatialState.position)
            .GetLength();
    requeue_gate.requeue_delay_frames = reQueueDelayFrames;
    requeue_gate.allow_movement_requeue = allowMovementReQueue;
    requeue_gate.allow_ball_control_requeue = allowBallControlReQueue;
    requeue_gate.allow_trap_requeue = allowTrapReQueue;
    requeue_gate.max_ball_control_requeue_frame = maxBallControlReQueueFrame;
    requeue_gate.min_remaining_movement_requeue_frames =
        minRemainingMovementReQueueFrames;
    requeue_gate.min_remaining_trap_requeue_frames =
        minRemainingTrapReQueueFrames;
    requeue_gate.now = now;
    if (!CastPlayer()->CanAttemptActionSelection(requeue_gate)) return false;
  }

  if (localInterruptAnim != e_InterruptAnim_ReQueue || action.Frame() > 12)
    CalculateFactualSpatialState();

  assert(command.desiredLookAt.coords[2] == 0.0f);

  // CREATE A CRUDE SET OF POTENTIAL ANIMATIONS

  CrudeSelectionQuery query = BuildCrudeSelectionQuery(command, tick, now, currentMentalImage);

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);
  if (dataSet.size() == 0) {
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
    } else
      return false;
  }

  // ANIM-1 A6.3b.2 / A6.4 shadow: keep the crude candidate set so the pure
  // Animation refinement can be compared with the legacy filter+sort result.
  DataSet crude_movement_snapshot;
  if (command.useDesiredMovement) crude_movement_snapshot = dataSet;
  // A6.4: the non-Movement branches set these (BallControl / Trap / Interfere).
  bool nm_strict = false;
  float nm_allowed_angle = 0.3f * pi;
  int nm_allowed_velocity_steps = 1;
  int nm_idle_level = 0;
  const bool pure_movement = command.desiredFunctionType == e_FunctionType_Movement &&
                               command.useDesiredMovement && !crude_movement_snapshot.empty();
  const bool pure_non_movement =
      command.desiredFunctionType != e_FunctionType_Movement &&
      command.useDesiredMovement && !crude_movement_snapshot.empty();

  //printf("dataset size after crude selection: %i\n", dataSet.size());

  if (command.useDesiredMovement) {

    Vector3 relDesiredDirection = command.desiredDirection.GetRotated2D(-spatialState.angle);
    float desiredAnimationVelocityFloat = command.desiredVelocityFloat;

    // // special case: 90 degrees cornering is tough with weak foot [hax version]
    // radian angle = command.desiredDirection.GetAngle2D(spatialState.directionVec);
    // if (command.desiredFunctionType == e_FunctionType_BallControl && adaptedDesiredVelocityFloat > dribbleVelocity && spatialState.floatVelocity > dribbleVelocity &&
    //     std::fabs(angle) > 0.3 * pi && std::fabs(angle) < 0.8 * pi && angle > 0) adaptedDesiredVelocityFloat = idleVelocity;

    SetMovementSimilarityPredicate(relDesiredDirection, FloatToEnumVelocity(desiredAnimationVelocityFloat));
    SetBodyDirectionSimilarityPredicate(command.desiredLookAt);

    if (command.desiredFunctionType == e_FunctionType_Movement) {
      // this makes body dirs lots better, at the cost of less correct movement anims. todo: maybe it's an idea to actually use this, and then allow more deviation in the physics code to fix the incorrect movement.
      //if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, false, 0.5f * pi);

      // ANIM-1 A6.3b.2: for pure Movement the candidate list now comes from the
      // stateless Animation refinement (see the pure_movement branch below), so
      // the legacy strict filters are skipped there.
      if (!pure_movement) {
        _KeepBestDirectionAnims(dataSet, command, true);
        if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, true);
      }
    }

    else if (command.desiredFunctionType == e_FunctionType_BallControl) {
      bool strict = true;
      if (CastPlayer()->AllowLastDitch()) {
        strict = false;
      }
      float allowedBaseAngle = 0.0f * pi;
      int allowedVelocitySteps = 0; // last ditch anims are always allowed > 0 velocity steps, as long as strict is false
      nm_strict = strict; nm_allowed_angle = allowedBaseAngle; nm_allowed_velocity_steps = allowedVelocitySteps;
      if (!pure_non_movement) {
        if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);
        _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps);
      }
    }

    else if (command.desiredFunctionType == e_FunctionType_Trap) {

      /*
      bool haste = false;
      // doesn't work well for long anims: they may be disregarded early on as
      panicky, yet then missed when we are starting to panic because they have a
      long 'fadein' if (currentAnim.functionType != e_FunctionType_Trap) {
       float hasteFactor = GetHasteFactor(false); if (hasteFactor
      > 0.5f) hasteFactor = true; std::string hasteString = hasteFactor ? "YES!
      PANIC!" : "nah relax bro";
      }*/

      bool strict = true;
      if (CastPlayer()->AllowLastDitch(false) || _HighOrBouncyBall(tick.ball)) strict = false;
      float allowedBaseAngle = 0.3f * pi;
      int allowedVelocitySteps = 2;
      int bestBallControlQuadrantID = -1;
      nm_strict = strict; nm_allowed_angle = allowedBaseAngle; nm_allowed_velocity_steps = allowedVelocitySteps;
      if (!pure_non_movement) {
        _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps, bestBallControlQuadrantID);
        if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);
      }

      // when too unlike command's desired movement, just don't go for it (and hope for another ballcontrol/trap anim to save us later on)
      if (!_HighOrBouncyBall(tick.ball) && query.allowLastDitchAnims == false) {
        assert(!dataSet.empty());
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        const AnimationClip &bestWeGot = GetBakedClip(*dataSet.begin());
        Vector3 bestWeGotMovement = bestWeGot.metadata.outgoing_movement.GetRotated2D(spatialState.angle);
        float currentDesiredDot = command.desiredDirection.GetDotProduct(spatialState.directionVec);

        bool allowAnim = true;

        radian angleDiff = std::fabs(bestWeGot.metadata.outgoing_direction.GetRotated2D(spatialState.angle).GetAngle2D(command.desiredDirection));
        if (angleDiff > 0.375f * pi) {
            // so we accept at least either 000 or 135 deg anims,
                          // which are two common anim types that are often
                          // available
          allowAnim = false;
        }

        Vector3 desiredBestDiff = bestWeGotMovement - desiredMovement;
        if ((desiredBestDiff.GetLength() > walkVelocity + 0.5f &&
             currentDesiredDot > 0.0f) ||
            (desiredBestDiff.GetLength() > sprintVelocity + 0.5f &&
             currentDesiredDot <= 0.0f)) {
            // + margin
          allowAnim = false;
        }

        if (!allowAnim) {
          return false;
        }
      }

    }

    else if (command.desiredFunctionType == e_FunctionType_Interfere) {

      bool strict = false;
      float allowedAngle = 0.3f * pi;
      int allowedVelocitySteps = 1;
      if (command.strictMovement == e_StrictMovement_True) strict = true;
      nm_strict = strict; nm_allowed_angle = allowedAngle; nm_allowed_velocity_steps = allowedVelocitySteps;

      if (!pure_non_movement) {
        _KeepBestDirectionAnims(dataSet, command, strict, allowedAngle, allowedVelocitySteps);
        if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedAngle);
      }
    }
  }

  if (!pure_movement && !pure_non_movement) {
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::ComparePriorityVariable, this, _1, _2));
  
    int desiredIdleLevel = 0;
    if (!tick.play_authorized) desiredIdleLevel = 2;
    if (tick.set_piece_active) desiredIdleLevel = 1;
    else if ((tick.ball.Predict(football::sim::TickSpan{20}).position - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
    SetIdlePredicate(desiredIdleLevel);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIdleVariable, this, _1, _2));
  
    // H3e4e2 measurement: strict counterfactual for the foot tie-break. The clone
    // is taken before the foot stable_sort and then receives exactly the same
    // remaining sorts, so the only difference between the two orders is foot.
    const bool foot_counterfactual =
        command.desiredFunctionType == e_FunctionType_Movement;
    DataSet withoutFootSort;
    if (foot_counterfactual) withoutFootSort = dataSet;
    SetFootSimilarityPredicate(spatialState.foot);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareFootSimilarity, this, spatialState.foot, _1, _2));
  
    if (command.desiredFunctionType != e_FunctionType_BallControl) {
      SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIncomingBodyDirectionSimilarity, this, _1, _2));
    }
  
    // moved down
    SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIncomingVelocitySimilarity, this, _1, _2));
  
    // OLD METHOD
    if (command.useDesiredTripDirection) {
      Vector3 relDesiredTripDirection = command.desiredTripDirection.GetRotated2D(-spatialState.angle);
      SetTripDirectionSimilarityPredicate(relDesiredTripDirection);
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareTripDirectionSimilarity, this, _1, _2));
    }
  
    // OLD METHOD
    if (command.desiredFunctionType != e_FunctionType_Movement) {
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
    }
  
    if (command.desiredFunctionType == e_FunctionType_Deflect) {
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareCatchOrDeflect, this, _1, _2));
    }
  
    if (foot_counterfactual) {
      // Same remaining chain, minus the foot sort. Conditions mirror production
      // so this stays a counterfactual and not a second scheduler.
      if (command.desiredFunctionType != e_FunctionType_BallControl) {
        SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&Humanoid::CompareIncomingBodyDirectionSimilarity, this, _1, _2));
      }
      SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&Humanoid::CompareIncomingVelocitySimilarity, this, _1, _2));
      if (command.useDesiredTripDirection) {
        SetTripDirectionSimilarityPredicate(command.desiredTripDirection.GetRotated2D(-spatialState.angle));
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&Humanoid::CompareTripDirectionSimilarity, this, _1, _2));
      }
      if (command.desiredFunctionType != e_FunctionType_Movement) {
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
      }
      if (command.desiredFunctionType == e_FunctionType_Deflect) {
        std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), std::bind(&Humanoid::CompareCatchOrDeflect, this, _1, _2));
      }
      if (!dataSet.empty() && !withoutFootSort.empty()) {
        RecordFootCounterfactual(animations_, *dataSet.begin(), *withoutFootSort.begin());
      }
    }
    MovementAnimationPerturbation &perturbation =
        MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied && foot_counterfactual &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        CastPlayer()->IsEligibleForProceduralLocomotion(tick.ball_retainer == player) &&
        !dataSet.empty() && !withoutFootSort.empty() &&
        dataSet.front() != withoutFootSort.front() &&
        (!perturbation.require_frame_count_difference ||
         GetBakedClip(dataSet.front()).frame_count !=
             GetBakedClip(withoutFootSort.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = CastPlayer()->GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = withoutFootSort.front();
      // Only reorder the single branch's local candidate set. SelectAnim runs
      // once; the saved baseline branch independently runs the normal order.
      dataSet.swap(withoutFootSort);
    }
  } else if (pure_non_movement) {
    // ANIM-1 A6.4 step 1d: the pure Animation refinement is the authority for
    // the non-Movement candidate list; the legacy filter+sort chain no longer
    // runs for non-Movement commands that request desired movement. The
    // pre-removal dual-run comparison recorded `nm checks=4 match=4
    // mismatch=0`, and the A6.3a final-clip oracle still guards end-to-end.
    SetIdlePredicate(nm_idle_level);
    SetFootSimilarityPredicate(spatialState.foot);
    MovementRefinementRequest nm_request;
    nm_request.rel_desired_direction = predicate_RelDesiredDirection;
    nm_request.desired_velocity = predicate_DesiredVelocity;
    nm_request.cornering_bias = predicate_CorneringBias;
    nm_request.look_at = predicate_LookAt;
    nm_request.position = spatialState.position;
    nm_request.angle = spatialState.angle;
    nm_request.rel_incoming_body_direction = spatialState.relBodyDirectionVec;
    nm_request.incoming_velocity = spatialState.enumVelocity;
    nm_request.desired_foot = spatialState.foot;
    nm_request.idle_level = nm_idle_level;
    nm_request.use_desired_look_at = command.useDesiredLookAt;
    nm_request.is_movement = false;
    nm_request.is_ball_control =
        (command.desiredFunctionType == e_FunctionType_BallControl);
    const Vector3 nm_trip_direction =
        command.desiredTripDirection.GetRotated2D(-spatialState.angle);
    const std::vector<int> nm_pure = RefineNonMovementCandidates(
        animations_, crude_movement_snapshot, nm_request, nm_strict, nm_allowed_angle,
        nm_allowed_velocity_steps, command.useDesiredTripDirection, nm_trip_direction);
    dataSet.assign(nm_pure.begin(), nm_pure.end());
    ++LocomotionPlaybackAuditState().nm_pure_ticks;
  } else {
    // ANIM-1 A6.3b.2: the pure Animation refinement is the authority for the
    // Movement candidate list; the legacy chain no longer runs for Movement.
    MovementRefinementRequest refine_request;
    refine_request.rel_desired_direction = predicate_RelDesiredDirection;
    refine_request.desired_velocity = predicate_DesiredVelocity;
    refine_request.cornering_bias = predicate_CorneringBias;
    refine_request.look_at = predicate_LookAt;
    refine_request.position = spatialState.position;
    refine_request.angle = spatialState.angle;
    refine_request.rel_incoming_body_direction = spatialState.relBodyDirectionVec;
    refine_request.incoming_velocity = spatialState.enumVelocity;
    refine_request.desired_foot = spatialState.foot;
    int desiredIdleLevel = 0;
    if (!tick.play_authorized) desiredIdleLevel = 2;
    if (tick.set_piece_active) desiredIdleLevel = 1;
    else if ((tick.ball.Predict(football::sim::TickSpan{20}).position - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
    refine_request.idle_level = desiredIdleLevel;
    refine_request.use_desired_look_at = command.useDesiredLookAt;
    refine_request.is_movement = true;

    // ANIM-1 A6.3b.3: the Player owns the Movement selection; the Humanoid
    // supplies the spatial request and reads back the chosen candidate list.
    dataSet = CastPlayer()->SelectMovement(animations_, crude_movement_snapshot,
                                           refine_request);
    if (dataSet.empty()) dataSet.push_back(GetIdleMovementAnimID());
    std::vector<int> without_foot = CastPlayer()->MovementCandidatesWithoutFoot();
    {// ANIM-1 A6.3b.2: count the stateless refinement as the active Movement path.
    auto& refine_audit = LocomotionPlaybackAuditState();
    ++refine_audit.refine_checks;
    ++refine_audit.refine_matches;
    }
    if (!dataSet.empty() && !without_foot.empty()) {
      RecordFootCounterfactual(animations_, dataSet.front(), without_foot.front());
    }
    MovementAnimationPerturbation &perturbation = MovementAnimationPerturbationAudit();
    if (perturbation.enabled && !perturbation.applied &&
        localInterruptAnim == e_InterruptAnim_Switch &&
        action.type == e_FunctionType_Movement &&
        CastPlayer()->IsEligibleForProceduralLocomotion(tick.ball_retainer == player) &&
        !dataSet.empty() && !without_foot.empty() &&
        dataSet.front() != without_foot.front() &&
        (!perturbation.require_frame_count_difference ||
         GetBakedClip(dataSet.front()).frame_count !=
             GetBakedClip(without_foot.front()).frame_count)) {
      perturbation.applied = true;
      perturbation.player_id = CastPlayer()->GetID();
      perturbation.time_ms = static_cast<int>(football::sim::ToMilliseconds(now));
      perturbation.original_anim_id = dataSet.front();
      perturbation.alternative_anim_id = without_foot.front();
      dataSet.swap(without_foot);
    }
  }

  int selectedAnimID = -1;
  football::sim::TouchChoice touch_choice;

  if (dataSet.size() == 0 &&
      command.desiredFunctionType == e_FunctionType_Movement) {
    dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
  }

  Vector3 desiredBodyDirectionRel = Vector3(0, -1, 0);

  // ANIM-1 A6.3b.2: for pure Movement the candidate list was already produced by
  // the stateless Animation refinement in the pure_movement branch above, which
  // also feeds the foot counterfactual. Nothing to install again here.
  if (command.useDesiredLookAt) desiredBodyDirectionRel = (command.desiredLookAt - (spatialState.position + spatialState.movement * 0.1f)).GetNormalized(Vector3(0, -1, 0)).GetRotated2D(-spatialState.angle);


  // LOCO-2.3b: for pure procedural locomotion, reorder the already-valid
  // Movement candidates so the motion-driven strategy's preferred clip wins,
  // without adding candidates or touching the non-Movement/touch paths.
  if (command.desiredFunctionType == e_FunctionType_Movement &&
      CastPlayer()->LocomotionAnimationAdaptation() &&
      CastPlayer()->IsEligibleForProceduralLocomotion(tick.ball_retainer == player) &&
      CastPlayer()->LocomotionAnimationInputSnapshot().has_value() && !dataSet.empty()) {
    const auto choice = SelectLocomotionAnimation(
        *CastPlayer()->LocomotionAnimationInputSnapshot(), animations_.Clips());
    // LOCO-2.3c: record the reference rate for calibration; frameNum is not
    // advanced by it here.
    auto& audit = LocomotionPlaybackAuditState();
    ++audit.samples;
    audit.reference_speed_sum += choice.reference_speed;
    audit.playback_rate_sum += choice.playback_rate;
    audit.playback_rate_max = std::max(audit.playback_rate_max, choice.playback_rate);
    // ANIM-1 A6.3a shadow oracle: is the motion-driven choice a legal legacy
    // Movement candidate? A non-zero disagreement means the new selector would
    // pick a clip the legacy filter rejects, blocking the A6.3b hand-over.
    ++audit.shadow_checks;
    // A6.3b.1: the final pick rule (preferred if a legal candidate, else the
    // legacy first) is a pure Animation helper; the Humanoid only installs it.
    const AnimationId pick = ResolveMovementPick(dataSet, choice.animation_id);
    auto preferred = std::find(dataSet.begin(), dataSet.end(),
                               static_cast<int>(choice.animation_id));
    if (preferred != dataSet.end()) ++audit.shadow_in_candidates;
    else ++audit.shadow_not_in_candidates;
    auto picked = std::find(dataSet.begin(), dataSet.end(), static_cast<int>(pick));
    if (picked != dataSet.end() && picked != dataSet.begin())
      std::rotate(dataSet.begin(), picked, std::next(picked));
    // Final-result oracle: the Movement branch takes `*dataSet.begin()` after the
    // reorder, so this is the clip that would actually play.
    if (*dataSet.begin() == static_cast<int>(choice.animation_id)) {
      ++audit.clip_matches;
    } else {
      ++audit.clip_mismatches;
      ++audit.fallback_count;
    }
  }

  if (command.desiredFunctionType == e_FunctionType_Movement ||
      command.desiredFunctionType == e_FunctionType_Trip ||
      command.desiredFunctionType == e_FunctionType_Special) {

    selectedAnimID = *dataSet.begin();
    Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
    assert(desiredMovement.coords[2] == 0.0f);
    Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
  } else if (command.desiredFunctionType == e_FunctionType_BallControl) {
    if (NeedTouch(now, tick, *dataSet.begin(), command, history)) {
      selectedAnimID = CastPlayer()->SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
    }
  } else if (command.desiredFunctionType == e_FunctionType_Trap ||
             command.desiredFunctionType == e_FunctionType_Interfere ||
             command.desiredFunctionType == e_FunctionType_Deflect) {
    selectedAnimID = CastPlayer()->SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, preferPassAndShot, allowPreTouchRotationSmuggle, useContinuousBallCheck);
  } else if (command.desiredFunctionType == e_FunctionType_ShortPass ||
             command.desiredFunctionType == e_FunctionType_LongPass ||
             command.desiredFunctionType == e_FunctionType_HighPass ||
             command.desiredFunctionType == e_FunctionType_Shot) {

    selectedAnimID = CastPlayer()->SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, false, allowPreTouchRotationSmuggle, useContinuousBallCheck);
  } else if (command.desiredFunctionType == e_FunctionType_Sliding) {
    selectedAnimID = CastPlayer()->SelectTouchAction(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice, localInterruptAnim, false, allowPreTouchRotationSmuggle, useContinuousBallCheck);
    if (selectedAnimID == -1) {
      if (dataSet.size() > 0) {
        selectedAnimID = *dataSet.begin();
            Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        assert(desiredMovement.coords[2] == 0.0f);
        Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, touch_choice.positions, touch_choice.rotation_smuggle);
      }
    }
  }

  // check if we really want to requeue; if the anim we dug up is actually better than the current

  // ANIM-1 A6.4 step 6b: the post-search ReQueue gate is a Player-owned pure
  // decision. It runs at the exact same point as the legacy check, after the
  // candidate search that may contain conditional work and RNG.
  {
    football::sim::CommitSelectionContext commit_gate;
    commit_gate.is_requeue = (localInterruptAnim == e_InterruptAnim_ReQueue);
    commit_gate.has_selection = (selectedAnimID != -1);
    commit_gate.current_positions_sufficient = currentAnim.positions.size() > 1;
    commit_gate.selected_positions_sufficient = touch_choice.positions.size() > 1;
    commit_gate.action_type = action.type;
    commit_gate.command_type = command.desiredFunctionType;
    if (selectedAnimID != -1) {
      commit_gate.current_outgoing_velocity =
          FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity);
      commit_gate.selected_outgoing_velocity =
          FloatToEnumVelocity(GetBakedClip(selectedAnimID).metadata.outgoing_velocity);
      commit_gate.current_quadrant = GetCurrentBakedClip().metadata.quadrant;
      commit_gate.selected_quadrant = GetBakedClip(selectedAnimID).metadata.quadrant;
      commit_gate.current_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(GetCurrentBakedClip().metadata.outgoing_angle);
      commit_gate.selected_forced_outgoing_angle =
          ForceIntoPreferredDirectionAngle(GetBakedClip(selectedAnimID).metadata.outgoing_angle);
    }
    if (!CastPlayer()->CanCommitActionSelection(commit_gate)) selectedAnimID = -1;
  }

  // make it so

  if (selectedAnimID != -1) {
    // ANIM-1 A6.4 step 7b: selection and installation are separate units, and
    // the Player now dispatches the installation.
    return CastPlayer()->CommitSelectedAction(
        selectedAnimID, touch_choice, command, tick, now, history,
        localInterruptAnim, material_candidate);
  }

  return false;
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
  currentAnim.movementSmuggle = CalculateMovementSmuggle(now, tick, command.desiredDirection, command.desiredVelocityFloat, history);
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
CrudeSelectionQuery Humanoid::BuildCrudeSelectionQuery(const PlayerCommand &command, const football::sim::PlayerTickContext &tick, football::sim::Tick now, MentalImage *current_mental_image) const {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = command.desiredFunctionType;

  query.byFoot = false;
  query.foot = spatialState.foot == e_Foot_Left ? e_Foot_Right : e_Foot_Left;

  // query.heedForcedFoot = true;
  // query.strongFoot = e_Foot_Right;

  // hax: long pass uses same anims as short pass
  if (query.functionType == e_FunctionType_LongPass) query.functionType = e_FunctionType_ShortPass;

  if (command.touchInfo.desiredPower != 0.0f) {
    query.byOutgoingBallDirection = true;
    query.outgoingBallDirection = command.touchInfo.desiredDirection.GetRotated2D(-spatialState.angle);
  }

  query.byIncomingVelocity = true;
  query.incomingVelocity = spatialState.enumVelocity;

  if (query.functionType != e_FunctionType_Movement && query.incomingVelocity == e_Velocity_Dribble) query.incomingVelocity = e_Velocity_Walk;

  query.incomingVelocity_Strict = false;
  if (query.functionType != e_FunctionType_Movement &&
      query.functionType != e_FunctionType_BallControl) {
    query.incomingVelocity_ForceLinearity = false;
    if (query.functionType != e_FunctionType_Deflect) {
      query.incomingVelocity_NoDribbleToSprint = true;
      if (query.functionType != e_FunctionType_ShortPass &&
          query.functionType != e_FunctionType_LongPass &&
          query.functionType != e_FunctionType_HighPass &&
          query.functionType != e_FunctionType_Shot) {
        query.incomingVelocity_ForceLinearity = true;
        query.incomingVelocity_NoDribbleToIdle = true;
      } else {  // passes and such
        query.incomingVelocity_ForceLinearity = false;
        query.incomingVelocity_NoDribbleToIdle = false;
      }
    } else {  // deflect
      query.incomingVelocity_NoDribbleToSprint = false;
      query.incomingVelocity_NoDribbleToIdle = false;
    }
  } else {
    query.incomingVelocity_Strict = true;
  }

  query.byIncomingBodyDirection = true;

  query.incomingBodyDirection = spatialState.relBodyDirectionVec;
  if (query.functionType != e_FunctionType_Movement) {
    query.incomingBodyDirection_Strict = false;
    if (query.functionType != e_FunctionType_Deflect) {
      if (query.functionType != e_FunctionType_BallControl) {
        query.incomingBodyDirection_ForceLinearity = true;
      } else {
        query.incomingBodyDirection_ForceLinearity = false; // new, we want to be able to use ballcontrol anims as trap more often to stop ball from rolling past us
      }
    } else {  // deflect
      query.incomingBodyDirection_ForceLinearity = false;
    }
  } else {
    query.incomingBodyDirection_Strict = true;
  }

  query.bySide = false;
  if (command.useDesiredLookAt &&
      GetCurrentBakedClip().metadata.outgoing_special_state.empty() &&
      tick.ball_retainer != CastPlayer()) {
    Vector3 playerLookAtVec = (command.desiredLookAt - spatialState.position).GetNormalized(spatialState.directionVec);
    query.lookAtVecRel = playerLookAtVec.GetRotated2D(-spatialState.angle);
    query.bySide = true;
  }

  if (command.onlyDeflectAnimsThatPickupBall == true) {
    query.byPickupBall = true;
    query.pickupBall = true;
  }

  if (command.desiredFunctionType == e_FunctionType_Trap ||
      command.desiredFunctionType == e_FunctionType_Interfere ||
      command.desiredFunctionType == e_FunctionType_Deflect) {
    query.byIncomingBallDirection = true;
    query.incomingBallDirection = (current_mental_image->GetBallPrediction(180, now, tick.ball) - current_mental_image->GetBallPrediction(120, now, tick.ball)).GetRotated2D(-spatialState.angle).GetNormalized(Vector3(0));
  }

  if (CastPlayer()->AllowLastDitch()) {
    query.allowLastDitchAnims = true;
  } else {
    query.allowLastDitchAnims = false;
  }

  if (command.desiredFunctionType == e_FunctionType_Trip) {
    query.byTripType = true;
    query.tripType = command.tripType;
  }

  query.properties.incoming_special_state = GetCurrentBakedClip().metadata.outgoing_special_state;
  if (tick.ball_retainer == player) query.properties.incoming_retain_state = GetCurrentBakedClip().metadata.outgoing_retain_state;
  if (command.useSpecialVar1) query.properties.special_var1 = command.specialVar1;
  if (command.useSpecialVar2) query.properties.special_var2 = command.specialVar2;

  if (!GetCurrentBakedClip().metadata.outgoing_special_state.empty()) query.incomingVelocity = e_Velocity_Idle; // standing up anims always start out idle
  return query;
}

bool Humanoid::NeedTouch(football::sim::Tick now, const football::sim::PlayerTickContext& tick, int animID, const PlayerCommand &command, std::span<MentalImage> history) {

  // when idle (and desiredvelo is idle as well), don't want to touch the ball every frame

  const AnimationClip &clip = GetBakedClip(animID);

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity != e_Velocity_Idle)) return true;
  if (command.desiredVelocityFloat > idleDribbleSwitch) return true;
  if (std::fabs(tick.ball.state().velocity.GetLength()) > 2.0f) return true;

  Vector3 animMovement = clip.metadata.outgoing_movement.GetRotated2D(spatialState.angle) * 0.3f + spatialState.movement * 0.7f;

  float animVelo = animMovement.GetLength();
  animMovement.Normalize(spatialState.directionVec);
  animMovement *= spatialState.movement.GetLength() * 0.8f + animVelo * 0.2f;
  auto currentMentalImage = football::sim::observation::SampleMentalImage(history, mentalImageTime);
  Vector3 ballMovement = (currentMentalImage->GetBallPrediction(250, now, tick.ball).Get2D() - currentMentalImage->GetBallPrediction(240, now, tick.ball).Get2D()) * 100;

  if (std::fabs(clip.metadata.outgoing_angle) > 0.125f * pi) return true;

  float distanceDeviation = (animMovement - ballMovement).GetLength();
  if (distanceDeviation >= 2.0) return true;

  float velocityDeviation = animMovement.GetLength() - ballMovement.GetLength(); // negative == ball is faster
  if (velocityDeviation < -1.4 || velocityDeviation >= 0.7) return true;

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity) != e_Velocity_Idle) {
    float angleDeviation = animMovement.GetNormalized(spatialState.directionVec).GetDotProduct(ballMovement.GetNormalized(spatialState.directionVec));
    if (angleDeviation < 0.975) return true;
  }

  return false;
}


Vector3 Humanoid::CalculateMovementSmuggle(football::sim::Tick now, const football::sim::PlayerTickContext& tick, const Vector3 &desiredDirection,
                                           float desiredVelocityFloat, std::span<MentalImage> history) {

  if (!enableMovementSmuggle) return Vector3(0);

  if (tick.own_team.DesignatedPlayer() != player || tick.designated_possession_player != player ||
      currentAnim.touchFrame != -1 || (currentAnim.functionType == e_FunctionType_Trip && GetCurrentBakedClip().metadata.trip_type != 1) || GetCurrentBakedClip().metadata.incoming_special_state.compare("") != 0 || GetCurrentBakedClip().metadata.outgoing_special_state.compare("") != 0 ||
      !tick.play_authorized || tick.set_piece_active || tick.ball_retainer != 0) return Vector3(0);

  Vector3 toDesired;

  // various stuff needed by all

  unsigned int timeToBall_ms = CastPlayer()->GetTimeNeededToGetToBall_ms();
  if (CastPlayer()->GetDesiredTimeToBall_ms() > (signed int)timeToBall_ms) {
    timeToBall_ms = CastPlayer()->GetDesiredTimeToBall_ms();
  }
  unsigned int animTime_ms = static_cast<int>(GetCurrentBakedClip().frame_count) * 10;
  unsigned int futureTime_ms = std::max(animTime_ms + defaultTouchOffset_ms, timeToBall_ms);

  Vector3 predictedOutgoingMovement = CalculateOutgoingMovement(currentAnim.positions);
  Vector3 predictedPos;
  radian predictedAngle;
  CalculatePredictedSituation(predictedPos, predictedAngle);
  Vector3 ballPos = football::sim::observation::SampleMentalImage(history, mentalImageTime)->GetBallPrediction(futureTime_ms, now, tick.ball);
  float ballHeight = ballPos.coords[2];
  Vector3 ffo = GetFrontOfFootOffsetRel(predictedOutgoingMovement.GetLength(), GetCurrentBakedClip().metadata.outgoing_body_angle, ballHeight).GetRotated2D(predictedAngle);
  Vector3 desiredBallPos = predictedPos + ffo;

  if (!CastPlayer()->HasPossession()) {

    // macro effect: consider a line going in the ball movement direction. consider the spot we want the ball at (in front of us) after this movement anim.
    // now calculate the shortest line between that line and that point. now move over that line from the point towards the line somewhat

    const Vector3 ballLineA = football::sim::observation::SampleMentalImage(history, mentalImageTime)->GetBallPrediction(0, now, tick.ball).Get2D();
    const Vector3 ballLineB = football::sim::observation::SampleMentalImage(history, mentalImageTime)->GetBallPrediction(futureTime_ms, now, tick.ball).Get2D();
    if ((ballLineA - ballLineB).GetLength() < 0.5f) return Vector3(0); // ball is slow or very close

    float u = SegmentProjectionParameter(ballLineA, ballLineB, desiredBallPos);
    Vector3 closestBallPos = ballLineA + (ballLineB - ballLineA) * u;

    toDesired = closestBallPos - desiredBallPos;

  } else {  // if HasPossession

    toDesired = ballPos.Get2D() - desiredBallPos;
  }

  unsigned int maxEffectTimeTreshold_ms = 250 + defaultTouchOffset_ms; // if the ball is this much longer 'farther away' than feasible, rather postpone effect until next anim (else we may overrun)
  if (FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity) == e_Velocity_Idle) maxEffectTimeTreshold_ms = 2000; // no danger of overrunning
  float maxEffectVelocity = dribbleWalkSwitch;
  float maxSmuggleMPS = 1.6f;

  if (futureTime_ms - (animTime_ms + defaultTouchOffset_ms) > maxEffectTimeTreshold_ms) return Vector3(0);

  Vector3 toDesiredMovement = toDesired / (animTime_ms * 0.001f);
  Vector3 resultingMovement = predictedOutgoingMovement + toDesiredMovement;
  float predictedVelocity = predictedOutgoingMovement.GetLength();
  float resultingVelocity = resultingMovement.GetLength();
  if (resultingVelocity > predictedVelocity && resultingVelocity > maxEffectVelocity) return Vector3(0);

  toDesired.NormalizeMax(maxSmuggleMPS * ((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) * 0.01f));

  //SetGreenDebugPilon(predictedPos);

  float removeDistance = 0.06f; // remove part of the smuggle (allow staying this far away from ideal spot)
  toDesired = toDesired.GetNormalized(0) * std::max(0.0f, toDesired.GetLength() - removeDistance);
  return toDesired;
}

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


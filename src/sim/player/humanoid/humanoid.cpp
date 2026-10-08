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

#include <cmath>

#include "foundation/geometry/line.hpp"
#include "sim/player/humanoid/humanoid_utils.hpp"

#include "sim/player/player_retain_anchor.hpp"

#include "sim/player/player.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/team/team.hpp"
#include "sim/match/match.hpp"
#include "sim/observation/mentalimage_sampling.hpp"
#include "sim/ball/ball_touch_application.hpp"
#include "sim/event/ball_touch_sink.hpp"
#include "sim/player/player_runtime_sink.hpp"
#include "sim/player/player_motion_constants.hpp"


#include "sim/player/kick_targeting.hpp"

#include "sim/animation/baked_selector.hpp"


using std::placeholders::_1;
using std::placeholders::_2;
constexpr bool animSmoothing = true;
constexpr float cheatFactor = 0.5f;
constexpr bool useContinuousBallCheck = true;

// 4b''-player-noquery-audit: provenance stays local to the selection queue.
// It is deliberately not part of PlayerCommand or serialized simulation state.
enum class PlayerPathSelectionCommandProvenance {
  Controller,
  LocalTrip,
  LocalTripMovementFallback
};
constexpr bool enableMovementSmuggle = true;
constexpr float cheatDiscardDistance = 0.02f; // don't 'display' this distance of cheat (looks better for small distances, but will look funny when too large, players 'missing' the ball and all. also has big influence on gameplay, since this influences player collisions etc)
constexpr float cheatDistanceBonus = 0.02f; // add extra allowed cheat distance (in meters). don't 'display' this distance of cheat (looks better for small distances, but will look funny when too large, players 'missing' the ball and all. also has big influence on gameplay, since this influences player collisions etc)
constexpr float cheatDiscardDistanceMultiplier = 0.4f; // lower == more snappy
constexpr float maxSmuggleDiscardDistance = 0.2f;
constexpr bool enableActionSmuggleDiscard = true;
constexpr bool forceFullActionSmuggleDiscard = false;
constexpr bool discardForwardSmuggle = true;
constexpr bool discardSidewaysSmuggle = false;
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

Humanoid::Humanoid(Player *player, const AnimationLibrary& animations, SimulationRng& rng)
    : HumanoidBase(player, player->GetTeam()->GetMatch(), animations, rng) {
  team = CastPlayer()->GetTeam();
}

Humanoid::~Humanoid() {}

Player *Humanoid::CastPlayer() const { return player; }

bool _PassFiddlingEnabled() {
  return true;
}

void Humanoid::Process(football::sim::Tick now, const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::BallTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink) {
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

  assert(match);
  Player* ball_retainer = tick.ball_retainer;
  // Tick-local publication: actors never reach Match for touch notification.
  const auto notify_touch = [&](e_TouchType type) {
    touch_sink.OnBallTouched({now, CastPlayer(), team, type});
  };

  bool instaDoorheb = false;
  if (tick.touches.last_team == team->GetID()) instaDoorheb = true;
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
      (tick.ball.Predict(0).Get2D() - tickStartState.position).GetLength();
  const bool designated_possession_player =
      tick.designated_possession_player == player;
  const bool designated_team_possession_player =
      team->GetDesignatedTeamPossessionPlayer() == player;
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
         ball_retainer, tick.pitch});
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

  currentAnim.frameNum++;
  CastPlayer()->StepSimulationAction(football::sim::TickSpan{1});
  const PlayerActionState &action =
      CastPlayer()->GetSimulationActionState();
  previousAnim_frameNum++;

  assert(team);
  int teamID = team->GetID();
  Team& first_roster = tick.first_processing_team;
  Team& second_roster = tick.second_processing_team;

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
    float actionDistance = ((spatialState.position + spatialState.movement * 0.1f) - tick.ball.Predict(100).Get2D()).GetLength();

    int team_id = tick.processing_slot;
    using namespace football::sim::player_timing;
    const auto phase = static_cast<std::uint64_t>(team_id);
    if (tick.designated_possession_player == player &&
        actionDistance < 3.0f) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwnerNear, football::sim::TickSpan{phase});

    } else if (tick.designated_possession_player == player) {
      frameNumPredicate = StaggeredRefreshDue(now, kOwner, football::sim::TickSpan{phase});

    } else if (team->GetDesignatedTeamPossessionPlayer() == player) {
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

  float ballDistanceNow = (tick.ball.Predict(0).Get2D() - spatialState.position).GetLength();
  float ballDistanceFuture = (tick.ball.Predict(200).Get2D() - (spatialState.position + spatialState.movement * 0.2f)).GetLength();
  float lastTouchBias = CastPlayer()->GetLastTouchBias(1500, now);
  float oppLastTouchBias = football::sim::event::TeamTouchBias(tick.touches,
      tick.opponent_team, 240, now);

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
      tick.ball.Predict(0).coords[2] < 1.6f) {

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
    Vector3 currentBallVec = tick.ball.GetMovement();
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

    football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
        first_roster, second_roster,
        now, ball_retainer);
    tick.ball.SetRotation(xRot, yRot, 0, 0.2f * (1.0f - bumpyRideBias), tick.ball_environment); // 0.9
    notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart)); //, e_TouchType_Accidental
  }
  // ---------------------- / EXPERIMENTAL ------------------------------------------------

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

    float fullBallDistance = (tick.ball.Predict(0) - (currentAnim.touchPos + currentAnim.positionOffset)).GetLength();

    if (!GetCurrentBakedClip().metadata.incoming_retain_state.empty()) {
      fullBallDistance = 0.0f;
      touchableDistance = 1.0f;
    }

    float bumpyRideBias = fullBallDistance / touchableDistance;
    bumpyRideBias = clamp(bumpyRideBias - 0.001f, 0.0f, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 0.5f);
    Vector3 currentBallVec = tick.ball.GetMovement();
    bool contact_reachable = false;
    if (contact_audit) {
      contact_audit->full_ball_distances.push_back(fullBallDistance);
      contact_audit->bumpy_ride_biases.push_back(bumpyRideBias);
      const float height_gap = std::fabs(
          desiredBallHeight - tick.ball.Predict(0).coords[2]);
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
        std::fabs(desiredBallHeight - tick.ball.Predict(0).coords[2]) <
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

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);
        record_contact_impulse(touchVec);
        tick.ball.SetRotation(xRot, yRot, 0, 0.5f * (1.0f - bumpyRideBias), tick.ball_environment);

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

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);
        record_contact_impulse(touchVec);
        tick.ball.SetRotation(xRot, yRot, 0, 0.6f * (1.0f - bumpyRideBias), tick.ball_environment); // 1.0

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
        football::sim::mechanics::GetPass(CastPlayer(), currentAnim.originatingCommand.desiredFunctionType, inputDirection, currentAnim.originatingCommand.touchInfo.inputPower, currentAnim.originatingCommand.touchInfo.autoDirectionBias, currentAnim.originatingCommand.touchInfo.autoPowerBias, tmpBallDirection, tmpBallPower, tmpTargetPlayer, currentAnim.originatingCommand.touchInfo.forcedTargetPlayer);
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
          team->SetDesignatedTeamPossessionPlayer(targetPlayer);

        }
        float zcurve = 0.0f;
        Vector3 touchVec = ballDirection * 36 * (ballPower + 0.3f);

        if (_PassFiddlingEnabled()) {
          if (contact_audit) ++contact_audit->pass_fiddling;
          //SetGreenDebugPilon(tick.ball.Predict(0).Get2D() + touchVec.Get2D() * 0.4f);

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

          //SetRedDebugPilon(tick.ball.Predict(0).Get2D() + touchVec.Get2D() * 0.4f);
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);
        record_contact_impulse(touchVec);
        float forwardness = 3.5f;
        if (currentAnim.functionType == e_FunctionType_HighPass) forwardness = -1.3f;
        radian xRot = touchVec.GetNormalized(0).coords[1] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        radian yRot = touchVec.GetNormalized(0).coords[0] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        tick.ball.SetRotation(xRot, yRot, zcurve, 0.9f * (1.0f - bumpyRideBias), tick.ball_environment);

        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_Shot) {

        // alter direction, if needed
        Vector3 ballDirection = currentAnim.originatingCommand.touchInfo.desiredDirection;
        Vector3 inputDirection = currentAnim.originatingCommand.touchInfo.inputDirection;
        Vector3 ballDirectionAltered = football::sim::mechanics::GetShotDirection(CastPlayer(), inputDirection, currentAnim.originatingCommand.touchInfo.autoDirectionBias);

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

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);
        record_contact_impulse(touchVec);
        tick.ball.SetRotation(xRot, yRot, zRot, 0.7f * (1.0f - bumpyRideBias), tick.ball_environment);
        notify_touch(GetTouchTypeForBodyPart(GetCurrentBakedClip().metadata.touch_bodypart));
      }

      else if (currentAnim.functionType == e_FunctionType_Interfere) {
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(&tick.ball, CastPlayer(), tick.touches, tick.opponent_team, now, rng_, GetCurrentBakedClip(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        touchVec =
            touchVec * 0.5f +
            (tick.ball.Predict(0).Get2D() - spatialState.position)
                    .GetNormalized() *
                4.0f +
            Vector3(0, 0, rng_.Uniform(0.5f, 1.5f));  // was 1 .. 6

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);
        // Legacy three-argument call: the third value is z rotation; bias was 1.0.
        tick.ball.SetRotation(xRot, yRot, 0.3f * (1.0f - bumpyRideBias), 1.0f, tick.ball_environment);
        notify_touch(e_TouchType_Accidental); // it's not truly accidental, but the resulting direction somewhat is, so goalies may fetch these balls
      }

      else if (currentAnim.functionType == e_FunctionType_Deflect) {
        bool canRetain = true; // can we grab hold of the ball?
        if (GetCurrentBakedClip().metadata.outgoing_retain_state.compare("") == 0) canRetain = false; // not the right anim, hopeless!
        if (ball_retainer != 0) canRetain = false; // somebody is already holding the ball :( (dafuq, this should not happen, right?)

        float veloDifficulty = NormalizedClamp((tick.ball.GetMovement() - spatialState.movement).GetLength(), 0.0f, 40.0f);
        float reactionDifficulty = 0.0f;
        Player *lastTouchPlayer = football::sim::event::LastTouchPlayer(tick.touches,
            tick.opponent_team);
        if (lastTouchPlayer) {
          reactionDifficulty =
              std::pow(lastTouchPlayer->GetLastTouchBias(
                           1200 - player->GetStat(football::model::PlayerStat::physical_reaction) * 400, now),
                       0.6f);
        }
        if ((1.0f - veloDifficulty) * (1.0f - reactionDifficulty) < 0.3f) canRetain = false; // too hard!

        if (canRetain) {
          runtime_sink.SetBallRetainer(CastPlayer());
          ball_retainer = CastPlayer();
        } else {
          Vector3 currentBallMovement = tick.ball.GetMovement().Get2D();
          Vector3 playerMovement = spatialState.movement;
          Vector3 touchVec =
              (-currentBallMovement * 0.1f + playerMovement * 2.0f +
               Vector3(-team->GetDynamicSide(), 0, 0) * 4.0f +
               Vector3(0, rng_.Uniform(-1, 1), 0))
                  .GetNormalized(0) *
              (currentBallMovement.GetLength() * 0.3f +
               playerMovement.GetLength() * 2.5f);
          touchVec.coords[2] += 1.2f;

          touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

          football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
              first_roster, second_roster,
              now, ball_retainer);
          tick.ball.SetRotation(0, 0, 0, 0.2f * (1.0f - bumpyRideBias), tick.ball_environment);
        }
        notify_touch(e_TouchType_Accidental);
      }

      else if (currentAnim.functionType == e_FunctionType_Sliding) {
        Vector3 touchVec = GetCurrentBakedClip().metadata.outgoing_ball_direction.GetRotated2D(spatialState.angle);
        touchVec = touchVec * 6.0f + tick.ball.GetMovement() * -0.28f;
        touchVec += Vector3(0, 0, 6);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, touchVec, history,
            first_roster, second_roster,
            now, ball_retainer);

        notify_touch(e_TouchType_Accidental);
      }
    }
    if (contact_audit && !contact_impulse_generated) {
      ++contact_audit->suppressed;
      if (contact_reachable) ++contact_audit->reachable_without_impulse;
    }
  }

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
      football::sim::ApplyBallTouch(tick.ball, tick.ball_environment, Vector3(0), history,
          first_roster, second_roster,
          now, ball_retainer);
      tick.ball.SetRotation(0, 0, 0, 1.0, tick.ball_environment);
      tick.ball.SetPosition(ComputeRetainAnchor(
          spatialState.position, spatialState.bodyDirectionVec, anchor), tick.ball_environment);
      notify_touch(e_TouchType_Intentional_Nonkicked);
    } else {
      // no longer retaining
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
    if (tick.ball.Predict(0).coords[1] > 20.05f) {
      OffsetPosition(Vector3(0, clamp(20.05f - tick.ball.Predict(0).coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (tick.ball.Predict(0).coords[1] < -20.05f) {
      OffsetPosition(Vector3(0, clamp(-20.05f - tick.ball.Predict(0).coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (tick.ball.Predict(0).coords[0] * -team->GetDynamicSide() >
        -pitchHalfW + 16.4f) {
      OffsetPosition(Vector3(clamp((-pitchHalfW + 16.4f) -
                                       tick.ball.Predict(0).coords[0] *
                                           -team->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -team->GetDynamicSide() * 0.3f);
    }
    if (tick.ball.Predict(0).coords[0] * -team->GetDynamicSide() <
        -pitchHalfW + 0.1f) {
      OffsetPosition(Vector3(clamp((-pitchHalfW + 0.1f) -
                                       tick.ball.Predict(0).coords[0] *
                                           -team->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -team->GetDynamicSide() * 0.4f);
    }
  }

  // next frame

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    //printf("size: %i\n", currentAnim.positions.size());
  } else {
  }
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
  CastPlayer()->BeginSimulationAction();


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
            .GetLength() > ballDistanceOptimizeThreshold) {
      return false;
    }
    if ((currentMentalImage->GetBallPrediction(defaultTouchOffset_ms, now, tick.ball).Get2D() -
         spatialState.position)
                .GetLength() > 2.0f &&
        //    tick.ball.GetMovement().GetNormalized(0).GetDotProduct(player->GetMovement().GetNormalizedMax(1.0f))
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
    if ((currentMentalImage->GetBallPrediction(1000, now, tick.ball) - tick.ball.Predict(1000)).GetLength() > 2.0f) return false;
  }

  // /optimizations

  if (localInterruptAnim == e_InterruptAnim_ReQueue) {

    float focusDistance = (tick.designated_possession_player->GetPosition() - spatialState.position).GetLength();

    if (action.type != e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        (CastPlayer()->HasPossession() || focusDistance > 12.0f)) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        action.Frame() + minRemainingMovementReQueueFrames >
            action.FrameCount() - 1) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        (!allowMovementReQueue || reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_BallControl &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (!allowBallControlReQueue ||
         action.Frame() > maxBallControlReQueueFrame ||
         reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_BallControl &&
        command.desiredFunctionType == e_FunctionType_Trap) return false;
    if (action.type == e_FunctionType_Trap &&
        command.desiredFunctionType == e_FunctionType_Trap &&
        (!allowTrapReQueue ||
         action.Frame() + minRemainingTrapReQueueFrames > action.ContactFrame() ||
         reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_Trap &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (!allowTrapReQueue ||
         action.Frame() + minRemainingTrapReQueueFrames > action.ContactFrame() ||
         reQueueDelayFrames > 0)) return false;

    // too similar to what we are already trying to accomplish
    if (currentAnim.originatingCommand.desiredFunctionType ==
            command.desiredFunctionType &&
        ((currentAnim.originatingCommand.desiredDirection *
          currentAnim.originatingCommand.desiredVelocityFloat) -
         (command.desiredDirection * command.desiredVelocityFloat))
                .GetLength() < 1.5f) {
      return false;
    }

    // requeue not needed?
    if ((action.type == e_FunctionType_Movement &&
         command.desiredFunctionType == e_FunctionType_Movement) ||
        (action.type == e_FunctionType_BallControl &&
         command.desiredFunctionType == e_FunctionType_BallControl) ||
        (action.type == e_FunctionType_Trap &&
         command.desiredFunctionType == e_FunctionType_BallControl) ||
        (action.type == e_FunctionType_Trap &&
         command.desiredFunctionType == e_FunctionType_Trap)) {

      // current change in momentum
      Vector3 plannedMomentumChange = currentAnim.outgoingMovement - currentAnim.incomingMovement;
      Vector3 desiredMomentumChange = (command.desiredDirection * command.desiredVelocityFloat) - spatialState.movement;

      if ((desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.0f &&
           desiredMomentumChange.GetDistance(plannedMomentumChange) < 4.0f) ||
          desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.8f ||
          desiredMomentumChange.GetDistance(plannedMomentumChange) < 2.0f) {
        return false;
      }
    }

    // don't requeue movement to ballcontrol halfway movement anims, unless there's a serious change of movement desired
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (now - CastPlayer()->GetLastTouchTick() <
             football::sim::TickSpan{60} &&
         CastPlayer()->GetLastTouchType() == e_TouchType_Intentional_Kicked) &&
        CastPlayer()->HasPossession()) {
        // && !CastPlayer()->AllowLastDitch()) {
      float desiredMovementChange = (spatialState.movement - (command.desiredDirection * command.desiredVelocityFloat)).GetLength();
      if (desiredMovementChange < 1.0f) return false;
    }
  }

  if (localInterruptAnim != e_InterruptAnim_ReQueue || action.Frame() > 12)
    CalculateFactualSpatialState();

  assert(command.desiredLookAt.coords[2] == 0.0f);

  // CREATE A CRUDE SET OF POTENTIAL ANIMATIONS

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
    query.incomingBallDirection = (currentMentalImage->GetBallPrediction(180, now, tick.ball) - currentMentalImage->GetBallPrediction(120, now, tick.ball)).GetRotated2D(-spatialState.angle).GetNormalized(Vector3(0));
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

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);
  if (dataSet.size() == 0) {
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
    } else
      return false;
  }

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

      // now strict-select from the remainder
      _KeepBestDirectionAnims(dataSet, command, true);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, true);
    }

    else if (command.desiredFunctionType == e_FunctionType_BallControl) {
      bool strict = true;
      if (CastPlayer()->AllowLastDitch()) {
        strict = false;
      }
      float allowedBaseAngle = 0.0f * pi;
      int allowedVelocitySteps = 0; // last ditch anims are always allowed > 0 velocity steps, as long as strict is false
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle); // needed for idle outgoing velo, probably
      _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps);
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
      _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps, bestBallControlQuadrantID);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);

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

      _KeepBestDirectionAnims(dataSet, command, strict, allowedAngle, allowedVelocitySteps);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedAngle);
    }
  }

  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::ComparePriorityVariable, this, _1, _2));

  int desiredIdleLevel = 0;
  if (!tick.play_authorized) desiredIdleLevel = 2;
  if (tick.set_piece_active) desiredIdleLevel = 1;
  else if ((tick.ball.Predict(200) - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
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

  int selectedAnimID = -1;
  std::vector<Vector3> positions_tmp;
  int touchFrame_tmp = -1;
  float radiusOffset_tmp = 0.0f;
  Vector3 touchPos_tmp;
  Vector3 fullActionSmuggle_tmp;
  Vector3 actionSmuggle_tmp;
  radian rotationSmuggle_tmp = 0;

  if (dataSet.size() == 0 &&
      command.desiredFunctionType == e_FunctionType_Movement) {
    dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
  }

  Vector3 desiredBodyDirectionRel = Vector3(0, -1, 0);
  if (command.useDesiredLookAt) desiredBodyDirectionRel = (command.desiredLookAt - (spatialState.position + spatialState.movement * 0.1f)).GetNormalized(Vector3(0, -1, 0)).GetRotated2D(-spatialState.angle);

  if (command.desiredFunctionType == e_FunctionType_Movement ||
      command.desiredFunctionType == e_FunctionType_Trip ||
      command.desiredFunctionType == e_FunctionType_Special) {

    selectedAnimID = *dataSet.begin();
    Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
    assert(desiredMovement.coords[2] == 0.0f);
    Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, rotationSmuggle_tmp);
  } else if (command.desiredFunctionType == e_FunctionType_BallControl) {
    if (NeedTouch(now, tick, *dataSet.begin(), command, history)) {
      selectedAnimID = GetBestCheatableAnimID(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim, preferPassAndShot);
    }
  } else if (command.desiredFunctionType == e_FunctionType_Trap ||
             command.desiredFunctionType == e_FunctionType_Interfere ||
             command.desiredFunctionType == e_FunctionType_Deflect) {
    selectedAnimID = GetBestCheatableAnimID(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim, preferPassAndShot);
  } else if (command.desiredFunctionType == e_FunctionType_ShortPass ||
             command.desiredFunctionType == e_FunctionType_LongPass ||
             command.desiredFunctionType == e_FunctionType_HighPass ||
             command.desiredFunctionType == e_FunctionType_Shot) {

    selectedAnimID = GetBestCheatableAnimID(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim);
  } else if (command.desiredFunctionType == e_FunctionType_Sliding) {
    selectedAnimID = GetBestCheatableAnimID(now, tick, history, dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim);
    if (selectedAnimID == -1) {
      if (dataSet.size() > 0) {
        selectedAnimID = *dataSet.begin();
            Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        assert(desiredMovement.coords[2] == 0.0f);
        Vector3 physicsVector = CalculatePhysicsVector(now, selectedAnimID, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, rotationSmuggle_tmp);
      }
    }
  }

  // check if we really want to requeue; if the anim we dug up is actually better than the current

  if (localInterruptAnim == e_InterruptAnim_ReQueue && selectedAnimID != -1 &&
      currentAnim.positions.size() > 1 && positions_tmp.size() > 1) {

    // don't requeue to same quadrant
    if (action.type == command.desiredFunctionType &&

        ((FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity) !=
              e_Velocity_Idle &&
          GetCurrentBakedClip().metadata.quadrant ==
              GetBakedClip(selectedAnimID).metadata.quadrant) ||
         ((FloatToEnumVelocity(GetCurrentBakedClip().metadata.outgoing_velocity) ==
               e_Velocity_Idle &&
           FloatToEnumVelocity(
               GetBakedClip(selectedAnimID).metadata.outgoing_velocity) ==
               e_Velocity_Idle) &&
          std::fabs((ForceIntoPreferredDirectionAngle(
                    GetCurrentBakedClip().metadata.outgoing_angle) -
                ForceIntoPreferredDirectionAngle(
                    GetBakedClip(selectedAnimID).metadata.outgoing_angle))) <
              0.06f * pi))) {

      selectedAnimID = -1;
    }
  }

  // make it so

  if (selectedAnimID != -1) {
    previousAnim_frameNum = currentAnim.frameNum;
    previousAnim_functionType = currentAnim.functionType;

    currentAnim.animationId = selectedAnimID;
    currentAnim.functionType = command.desiredFunctionType;
    currentAnim.frameNum = 0;
    currentAnim.touchFrame = touchFrame_tmp;
    currentAnim.originatingInterrupt = localInterruptAnim;
    currentAnim.touchPos = touchPos_tmp;
    currentAnim.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, spatialState.relBodyAngleNonquantized - GetCurrentBakedClip().metadata.incoming_body_angle) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
    currentAnim.rotationSmuggle.end = rotationSmuggle_tmp;
    currentAnim.rotationSmuggleOffset = 0;
    currentAnim.actionSmuggle = actionSmuggle_tmp;
    currentAnim.actionSmuggleOffset = Vector3(0);
    currentAnim.actionSmuggleSustain = Vector3(0); // calculated below
    currentAnim.actionSmuggleSustainOffset = Vector3(0);
    currentAnim.movementSmuggle = Vector3(0); // needs to be reset here, else the previous calc is used in upcoming 'calculatemovementsmuggle'
    currentAnim.movementSmuggleOffset = Vector3(0);
    currentAnim.incomingMovement = spatialState.movement;
    currentAnim.outgoingMovement = CalculateOutgoingMovement(positions_tmp);
    currentAnim.positions.clear();
    currentAnim.positions.assign(positions_tmp.begin(), positions_tmp.end());
    currentAnim.positionOffset = Vector3(0);
    currentAnim.originatingCommand = command;
    RecordMovementCommandAcceptance(material_candidate, action.type,
                                    static_cast<int>(football::sim::ToMilliseconds(action.elapsed)), localInterruptAnim,
                                    command, static_cast<int>(GetCurrentBakedClip().frame_count));
    currentAnim.movementSmuggle = CalculateMovementSmuggle(now, tick, command.desiredDirection, command.desiredVelocityFloat, history);
    currentAnim.movementSmuggleOffset = Vector3(0);
    CastPlayer()->BeginSimulationAction();
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
    return true;
  }

  return false;
}

bool Humanoid::NeedTouch(football::sim::Tick now, const football::sim::PlayerTickContext& tick, int animID, const PlayerCommand &command, std::span<MentalImage> history) {

  // when idle (and desiredvelo is idle as well), don't want to touch the ball every frame

  const AnimationClip &clip = GetBakedClip(animID);

  if (FloatToEnumVelocity(clip.metadata.outgoing_velocity != e_Velocity_Idle)) return true;
  if (command.desiredVelocityFloat > idleDribbleSwitch) return true;
  if (std::fabs(tick.ball.GetMovement().GetLength()) > 2.0f) return true;

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

float Humanoid::GetBodyBallDistanceAdvantage(int animID, e_FunctionType functionType, const Vector3 &animTouchMovement, const Vector3 &touchMovement, const Vector3 &incomingMovement, const Vector3 &outgoingMovement, radian outgoingAngle, /*const Vector3 &animBallToBall2D, */const Vector3 &bodyPos, const Vector3 &FFO, const Vector3 &animBallPos2D, const Vector3 &actualBallPos2D, const Vector3 &ballMovement2D, float radiusFactor, float radiusCheatDistance, float decayPow, bool debug) const {

  assert(touchMovement.coords[2] == 0.0f);
  assert(bodyPos.coords[2] == 0.0f);

  const AnimationClip &clip = GetBakedClip(animID);

  // super simplistic debug version
  //if (animBallPos2D.GetDistance(actualBallPos2D) < radiusFactor * 1.0) return 1.0f; else return 0.0f;

  float touchVelocity = touchMovement.GetLength();
  float animTouchVelocity = animTouchMovement.GetLength();
  float highestTouchVelocity = (animTouchVelocity > touchVelocity) ? animTouchVelocity : touchVelocity;

  float incomingVelocity = incomingMovement.GetLength();
  float outgoingVelocity = outgoingMovement.GetLength();
  float averageInOutVelocity = (incomingMovement + outgoingMovement).GetLength() * 0.5f;
  float highestInOutVelocity = (incomingVelocity > outgoingVelocity) ? incomingVelocity : outgoingVelocity;

  float highestVelocity = (highestTouchVelocity > highestInOutVelocity) ? highestTouchVelocity : highestInOutVelocity;

  float velocityChange = outgoingVelocity - incomingVelocity;
  float velocityChange_mps = velocityChange / (static_cast<int>(clip.frame_count) * 0.01f);

  float bodyAnimBallBonus = 1.0f - curve(NormalizedClamp(((bodyPos + FFO.GetNormalized(0) * 0.1f) - animBallPos2D).GetLength(), 0.0f, 0.7f), 0.7f); // less FFO feels better
  float bodyActualBallBonus = 1.0f - curve(NormalizedClamp(((bodyPos + FFO.GetNormalized(0) * 0.1f) - actualBallPos2D).GetLength(), 0.0f, 0.7f), 0.4f);
  float velocityBonus = 1.0f - NormalizedClamp(averageInOutVelocity, idleVelocity, sprintVelocity);
  float velocityChangeBonus = 1.0f - NormalizedClamp(velocityChange_mps / 20.0f, -1.0f, 1.0f);

  float radius = radiusFactor;
  radius *= 1.0f +
            1.0f * bodyAnimBallBonus +
            0.6f * bodyActualBallBonus +
            0.8f * bodyAnimBallBonus * bodyActualBallBonus +
            0.0f * velocityChangeBonus +
            1.0f * velocityBonus;

  // more cheat for faster balls. rationale: with the current system, points in time are used as 'check if legal ball touch', while in reality, this is a continuum.
  // in other words, faster balls have an unrealistic disadvantage in this system. this tries to restore this balance. however; this widens the ball touch area, while
  // a better solution would be to actually somehow increase the number of checked points (or rather, to just check against the continuum instead of against points)
  //radius *= 0.5f + clamp((ballMovement2D - touchMovement).GetLength() * 0.1f, 0.0f, 1.5f);

  float effectiveRadiusCheatDistance = radiusCheatDistance;

  Vector3 outgoingDirection;
  if (FloatToEnumVelocity(outgoingMovement.GetLength()) == e_Velocity_Idle) {
    outgoingDirection = Vector3(0, -1, 0).GetRotated2D(outgoingAngle);
  } else {
    outgoingDirection = outgoingMovement.GetNormalized();
  }

  Vector3 behindVectorUnscaled = -(incomingMovement * 0.1f + touchMovement * 0.2f + outgoingMovement * 0.7f);
  Vector3 behindVector =
      behindVectorUnscaled.GetNormalized(0) *
      std::pow(
          NormalizedClamp(behindVectorUnscaled.GetLength(), 0, sprintVelocity),
          0.5f);

  // cornering with a more centered behindvector for 'richer' range
  float dot = Vector3(0, -1, 0).GetDotProduct(outgoingDirection);
  dot = 0.5f + clamp(dot * 0.5f + 0.5f, 0.0f, 1.0f) * 0.5f;
  behindVector *= dot;

  Vector3 animToActualBall = actualBallPos2D - animBallPos2D;

  bool deformArea = true;
  if (deformArea) {
    Vector3 straightAngleVectorUnscaled = incomingMovement;
    Vector3 straightAngleVector = straightAngleVectorUnscaled.GetNormalized(outgoingDirection);
    radian toStraightAngle = Vector3(0, -1, 0).GetAngle2D(straightAngleVector);

    // rotate animball to actualball vec into outgoing direction so we can do funky stuff with it
    animToActualBall.Rotate2D(toStraightAngle);

    // cheating to the side (lateral) (as seen from the outgoing direction vector) is harder at high velos (effectively changes cheat circle into ellipse)
    float lateralRadiusFactor =
        0.6f -
        0.3f * std::pow(NormalizedClamp(straightAngleVectorUnscaled.GetLength(),
                                        idleVelocity, sprintVelocity),
                        0.7f);
    animToActualBall.coords[0] /= lateralRadiusFactor; // coords[0] is now the lateral component
    radius *= std::pow(
        1.0f / lateralRadiusFactor,
        0.5f);  // make sure we stick with the same surface area (= pi * r ^ 2,
                // so just take the sqrt of lateralradiusfactor, which is
                // effectively a surface area multiplier)
    /*
    // brick wall: if balls are beyond animtouchpos in outgoingDirection
    territory, cut off at higher speeds and such if (animToActualBall.coords[1]
    < 0.0f) {
      //float brickWallDistanceFactor = 1.0f -
    NormalizedClamp(averageInOutVelocity, 0.0f, sprintVelocity);
      //float brickWallDistance = radiusFactor * brickWallDistanceFactor * 2.0f;
      //animToActualBall.coords[1] *= 1.0f + std::pow(averageInOutVelocity, 1.5f)
    * radiusFactor * 5.0f; animToActualBall.coords[1] *= 1.0f +
    std::pow(averageInOutVelocity, 1.5f) * radiusFactor * 0.4f;
    }
    */
    // rotate back and act like nothing happened
    animToActualBall.Rotate2D(-toStraightAngle);
  }

  // do some magic

  Vector3 adaptedActualBallPos2D = animBallPos2D + animToActualBall;

  float radiusCheatBehindBias = 0.6f; // how much the radiuscheat heeds behindvec
  Vector3 behindCenter = animBallPos2D + behindVector * (radius + (effectiveRadiusCheatDistance * radiusCheatBehindBias)) * cheatFactor;

  float result = 1.0f;
  float allowedRadius = (radius + effectiveRadiusCheatDistance) * cheatFactor + cheatDistanceBonus;
  if (adaptedActualBallPos2D.GetDistance(behindCenter) > allowedRadius) {
    result = 0.0f;
  }

  return result;
}

signed int Humanoid::GetBestCheatableAnimID(football::sim::Tick now, const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, const DataSet &sortedDataSet, bool useDesiredMovement, const Vector3 &desiredDirection, float desiredVelocityFloat, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, int &animTouchFrame_ret, float &radiusOffset_ret, Vector3 &touchPos_ret, Vector3 &fullActionSmuggle_ret, Vector3 &actionSmuggle_ret, radian &rotationSmuggle_ret, e_InterruptAnim localInterruptAnim, bool preferPassAndShot) const {

  // never allow touchanims when someone else is holding the ball in his/her hands
  if (tick.ball_retainer != 0 && tick.ball_retainer != player) return -1;

  Vector3 incomingMovement = spatialState.movement.GetRotated2D(-spatialState.angle);

  signed int bestAnimID = -1;
  Vector3 bestActionSmuggleVec2D;
  Vector3 bestTouchMovementAbs;

  DataSet::const_iterator iter = sortedDataSet.begin();

  Vector3 desiredMovement = desiredDirection * desiredVelocityFloat;

  float playerHeight = player->GetModel().height;

  e_FunctionType functionType = StringToFunctionType(
      static_cast<e_DefString>(GetBakedClip(*iter).metadata.action_type));

  radian rotationSmuggle_ret_tmp = 0;
  radian predictedAngle = 0;
  Vector3 adaptedOutgoingMovement;

  Vector3 dud;

  bool found = false;
  while (iter != sortedDataSet.end() && found == false) {

    const AnimationClip &clip = GetBakedClip(*iter);
    bool isBase = clip.metadata.base_animation;

    const std::vector<Vector3> &origPositionCache = clip.root_positions;

    Vector3 physicsVector = CalculatePhysicsVector(now, *iter, useDesiredMovement, desiredMovement, useDesiredBodyDirection, desiredBodyDirectionRel, positions_ret, rotationSmuggle_ret_tmp);

    // anim space!
    predictedAngle = clip.metadata.outgoing_angle + rotationSmuggle_ret_tmp;
    predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);

    // iterate all possible touches of this anim
    int touchNum = 0;
    Vector3 animBallPos;
    int animTouchFrame = 0;

    Quaternion animBodyRot;
    Vector3 animBodyPos;
    Vector3 bodyPos;
    Vector3 prevBodyPos;

    Vector3 outgoingMovement = CalculateOutgoingMovement(positions_ret).GetRotated2D(-spatialState.angle);
    adaptedOutgoingMovement = outgoingMovement; // may be changed into touchMovement later on, when an anim is found

    int frameCount = static_cast<int>(clip.frame_count) - 1;

    const int totalTouches = static_cast<int>(clip.touches.size());
    int touchIDs[totalTouches];
    int count = 0;

    int defaultTouchFrame = clip.metadata.touch_frame;
    assert(defaultTouchFrame >= 0 && defaultTouchFrame < frameCount);

    // first the middle one down to the first
    for (int i = totalTouches / 2; i > -1; i--) {
      touchIDs[count] = i;
      count++;
    }
    // then the 1-after-middle one and upwards
    for (int i = totalTouches / 2 + 1; i < totalTouches; i++) {
      touchIDs[count] = i;
      count++;
    }

    while (touchNum < totalTouches && found == false) {

      animBallPos = clip.touches[touchIDs[touchNum]].position;
      animTouchFrame = clip.touches[touchIDs[touchNum]].frame;

      // out of bounds?
      if (tick.ball_retainer != player) {
        Vector3 absBallPos = tick.ball.Predict(animTouchFrame * 10);
        if (std::fabs(absBallPos.coords[0]) > pitchHalfW + lineHalfW + 0.11f ||
            std::fabs(absBallPos.coords[1]) > pitchHalfH + lineHalfW + 0.11f) {
          touchNum++;
          continue;
        }
      }

      Vector3 touchMovement = CalculateMovementAtFrame(positions_ret, animTouchFrame).GetRotated2D(-spatialState.angle);
      Vector3 animTouchMovement = CalculateMovementAtFrame(origPositionCache, animTouchFrame);// already anim space so no: .GetRotated2D(-spatialState.angle);

      Vector3 ballPos, ballMovement;
      auto mentalImage = football::sim::observation::SampleMentalImage(history, mentalImageTime);
      ballPos = mentalImage->GetBallPrediction(animTouchFrame * 10, now, tick.ball);
      ballMovement = (mentalImage->GetBallPrediction(animTouchFrame * 10 + 10, now, tick.ball) - mentalImage->GetBallPrediction(animTouchFrame * 10, now, tick.ball)) * 100.0f;
      ballPos = (ballPos - spatialState.position).GetRotated2D(-spatialState.angle);
      ballMovement = ballMovement.GetRotated2D(-spatialState.angle);

      bodyPos = positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      bodyPos.coords[2] = 0;

      const auto &touchPose = clip.poses[animTouchFrame];
      animBodyRot = touchPose.orientations[BodyPart::player];
      animBodyPos = touchPose.positions[BodyPart::player];

      real x, y, z;
      animBodyRot.GetAngles(x, y, z);

      float animBallHeight = animBallPos.coords[2];
      if (allowPreTouchRotationSmuggle) {
        animBallPos = (animBallPos - animBodyPos).GetRotated2D(rotationSmuggle_ret_tmp * ((float)animTouchFrame / (float)frameCount)) + positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      } else {
        animBallPos = (animBallPos - animBodyPos) + positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      }
      animBallPos.coords[2] = animBallHeight * (playerHeight / defaultPlayerHeight);

      // now pick the ballPos from the previous 9ms that is closest to animBallPos. this is to emulate a continuous 'close enough?'-check instead of a 'single moment' check.
      if (useContinuousBallCheck) {
        Line ballLine(ballPos - ballMovement * 0.006f, ballPos + ballMovement * 0.003f);
        float u = clamp(ballLine.GetClosestToPoint(animBallPos), 0.0f, 1.0f);
        ballPos = ballLine.GetVertex(0) + (ballLine.GetVertex(1) - ballLine.GetVertex(0)) * u;
      }

      Vector3 actionSmuggleVec3D = ballPos - animBallPos;
      Vector3 actionSmuggleVec2D = actionSmuggleVec3D.Get2D();

      // ball height

      float ballDistanceZ = std::fabs(actionSmuggleVec3D.coords[2]);

      ballDistanceZ *= 1.0f - clamp((animBallPos.coords[2] - 0.11) * 0.3f, 0.0f, 0.2f); // higher balls == cheat more Z (else we would have to make 100000000 anims for high balls on different heights)
      ballDistanceZ *= 1.0f - clamp((ballPos.coords[2] - 0.11) * 0.4f, 0.0f, 0.3f);
      // enforce maximum height though
      if (ballPos.coords[2] > 1.8f && ballPos.coords[2] > animBallPos.coords[2] + 0.12f) ballDistanceZ *= 2.0f;
      if (ballPos.coords[2] > 2.6f && ballPos.coords[2] > animBallPos.coords[2] + 0.08f) ballDistanceZ *= 20.0f;
      if (functionType == e_FunctionType_Deflect) ballDistanceZ *= 0.8f;
      if (tick.ball_retainer == player) ballDistanceZ = 0.0f;

      if (ballPos.coords[2] < 0.5f && isBase) ballDistanceZ = std::max(ballDistanceZ - 0.15f, 0.0f); // low balls should be doable with ground level anims, doesn't look that bad :P

      if (ballDistanceZ < 0.22f) {

        // default touch can be 'cheated' towards best, has biggest 'radius'
        float touchFrameAwkwardness = NormalizedClamp(std::abs(defaultTouchFrame - animTouchFrame), 0.0f, 4.0f);
        touchFrameAwkwardness = std::pow(touchFrameAwkwardness, 2.0f) * 0.5f;

        /* todo: check if this is a possibility in some form
              // add this to desired cheatvec to get more of a flowing feeling to it
              Vector3 flowVector = spatialState.movement - tick.ball.GetMovement().Get2D();// - anims->GetAnim(*iter)->GetOutgoingMovement().GetRotated2D(spatialState.angle) * 2.0;
              flowVector.Rotate2D(-spatialState.angle);
              actionSmuggleVec2D += (flowVector).GetNormalized(0) * 0.3f;
        */

        float touchFrameFactor = animTouchFrame / 24.0f; // makes it 0.5 around the 'average touchframe' (educated guess average)
        touchFrameFactor = std::pow(
            touchFrameFactor,
            0.7f);  // advantage for anims that touch the ball earlier.
                    // rationale: more cheating, sure, but for a shorter period
        // even steeper falloff after 1.0
        if (touchFrameFactor > 1.0f) touchFrameFactor = 1.0f + ((touchFrameFactor - 1.0f) * 0.5f);

        float decayPow = 1.0f;
        float radiusCheatOffset = 0.0f;
        float radiusFactor = 0.3f * (1.0f - touchFrameAwkwardness);

        if (functionType == e_FunctionType_Deflect) {
          radiusFactor *= 1.8f;
          radiusCheatOffset += 0.4f;
        }

        if (functionType == e_FunctionType_Sliding) {
          radiusFactor *= 0.2f;
          radiusCheatOffset = 0.0f;
        }  // prefer sliding without ball touch
        if (functionType == e_FunctionType_Interfere) {
          radiusFactor *= 1.4f;
          radiusCheatOffset += 0.2f;
        }

        if (functionType == e_FunctionType_ShortPass) {
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_LongPass) {
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_HighPass) {
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_Shot) {
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }

        if ((functionType == e_FunctionType_Trap ||
             functionType == e_FunctionType_BallControl) &&
            preferPassAndShot == true) {
          radiusFactor *= 0.3f;
        }

        radiusCheatOffset *= (1.0f - touchFrameAwkwardness);

        float touchVelo = touchMovement.GetLength();

        Vector3 FFO = GetFrontOfFootOffsetRel(touchVelo, z, ballPos.coords[2]);
        if (FloatToEnumVelocity(touchVelo) == e_Velocity_Idle) {
          //FFO.Rotate2D(z); // always towards y = -1, right? (hmmm not really)
        } else {
          FFO.Rotate2D(FixAngle(touchMovement.GetNormalized(Vector3(0, -1, 0)).GetAngle2D()));
        }
        // just touched ball
        float lastTouchBias = curve(player->GetLastTouchBias(600, now + football::sim::TickSpan{static_cast<std::uint64_t>(animTouchFrame)}), 1.0f);
        if (lastTouchBias > 0.0f) {
          float factor = 1.0f - lastTouchBias * 0.97f * (1.0f - player->GetStat(football::model::PlayerStat::technical_ballcontrol) * 0.1f);
          radiusFactor *= factor;
          radiusCheatOffset *= factor;
        }

        bool debug = false;

        float touchFramedRadiusFactor = radiusFactor * touchFrameFactor;
        float bodyBallDistanceAdvantage = GetBodyBallDistanceAdvantage(*iter, functionType, animTouchMovement, touchMovement, incomingMovement, adaptedOutgoingMovement, predictedAngle, bodyPos, FFO, animBallPos.Get2D(), ballPos.Get2D(), ballMovement.Get2D(), touchFramedRadiusFactor, radiusCheatOffset, 1.0f, debug);

        if (bodyBallDistanceAdvantage >= 1.0f ||
            tick.ball_retainer == player) {
          found = true;

          bestAnimID = (signed int)*iter;
          bestActionSmuggleVec2D = actionSmuggleVec2D;
          bestTouchMovementAbs = touchMovement.GetRotated2D(spatialState.angle);
          animTouchFrame_ret = animTouchFrame;
          radiusOffset_ret = 1000.0f;//radiusOffset; todo? is this still in use?
        }
      }
      touchNum++;
    }
    iter++;
  }

  if (found) {
    auto currentMentalImage = football::sim::observation::SampleMentalImage(history, mentalImageTime);
    touchPos_ret = currentMentalImage->GetBallPrediction(animTouchFrame_ret * 10, now, tick.ball);

    fullActionSmuggle_ret = bestActionSmuggleVec2D.GetRotated2D(spatialState.angle);
    actionSmuggle_ret = fullActionSmuggle_ret;

    if (forceFullActionSmuggleDiscard) {

      actionSmuggle_ret = 0;

    } else if (enableActionSmuggleDiscard) {

      // cheat discard distance (don't show some amount of cheat, like, a cheat-cheat :D CHEATCEPTION)
      float smuggleDistance = actionSmuggle_ret.GetLength();
      float adaptedCheatDiscardDistanceMultiplier = cheatDiscardDistanceMultiplier;
      if (functionType != e_FunctionType_BallControl) adaptedCheatDiscardDistanceMultiplier *= 0.8f;
      if (touchPos_ret.coords[2] > 0.5f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
      if (touchPos_ret.coords[2] > 1.0f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
      float cheatDiscardDistanceBonus = 0.0f;
      if (functionType == e_FunctionType_Interfere) cheatDiscardDistanceBonus += 0.1f; // don't break flow

      // smuggle, in this context, is how much we will move to the ball after the physics have been accounted for.
      // this is so it seems like we actually touch the ball. we can discard this smuggle somewhat though to make the physics look better.
      // it always is a trade-off between visually touching the ball and visually moving 'correctly', physics-wise.

      // method 1: always discard a part of the smuggle distance
      smuggleDistance = clamp(smuggleDistance - (cheatDiscardDistance + cheatDiscardDistanceBonus), 0.0f, 100.0);
      smuggleDistance *= 1.0f - adaptedCheatDiscardDistanceMultiplier;

      smuggleDistance = std::min(smuggleDistance, actionSmuggle_ret.GetLength() - maxSmuggleDiscardDistance);

/* deprecated
      // method 2: force maximum smuggle - discard the rest
      float smuggleDistanceMPS = smuggleDistance / (animTouchFrame_ret / 100.0f);
      float maximumSmuggleMPSFactor = 1.0;
      if (functionType == e_FunctionType_Sliding) maximumSmuggleMPSFactor *= 4.0f;
      if (functionType == e_FunctionType_Deflect) maximumSmuggleMPSFactor *= 3.0f;
      //smuggleDistanceMPS = clamp(smuggleDistanceMPS, 0.0f, maximumSmuggleMPS * (1.0f / adaptedCheatDiscardDistanceMultiplier) * maximumSmuggleMPSFactor);
      smuggleDistanceMPS = clamp(smuggleDistanceMPS, 0.0f, maximumSmuggleMPS * maximumSmuggleMPSFactor);
      smuggleDistance = smuggleDistanceMPS * (animTouchFrame_ret / 100.0f);
*/

      if (tick.ball_retainer == player) smuggleDistance = 0.0f;
      actionSmuggle_ret = actionSmuggle_ret.GetNormalized(0) * smuggleDistance;

      // lose forward-facing part of smuggle

      if (discardForwardSmuggle || discardSidewaysSmuggle) {

        radian toStraightAngle = spatialState.angle + predictedAngle;
        actionSmuggle_ret.Rotate2D(-toStraightAngle);

        if (discardForwardSmuggle) {
          float shortenForwardDistance = 0.02f;
          float allowForwardDistance =
              0.25f *
              (1.0f -
               std::pow(NormalizedClamp(adaptedOutgoingMovement.GetLength(),
                                        idleVelocity, sprintVelocity - 2.0f),
                        0.6f)) *
              (animTouchFrame_ret * 0.1f);
          if (functionType != e_FunctionType_BallControl && functionType != e_FunctionType_Trap) allowForwardDistance += 0.1f; // allow pass/shot/intefere anims and such to smuggle forwards more, since their follow-up-movement doesn't matter that much
          if (actionSmuggle_ret.coords[1] < 0.0f) actionSmuggle_ret.coords[1] = clamp(actionSmuggle_ret.coords[1] + shortenForwardDistance, -allowForwardDistance, 0.0f);
        }
        if (discardSidewaysSmuggle) {
          actionSmuggle_ret.coords[0] *= 0.7f;
        }

        actionSmuggle_ret.Rotate2D(toStraightAngle);
      }

      // less chaos in micro battles
      actionSmuggle_ret *= 0.7f + 0.3f * NormalizedClamp(CastPlayer()->GetClosestOpponentDistance(tick.opponent_team), 0.6f, 1.2f);
    }

    assert(actionSmuggle_ret.coords[2] == 0.0f);
  }

  rotationSmuggle_ret = rotationSmuggle_ret_tmp;

  return bestAnimID;
}

Vector3 Humanoid::CalculateMovementSmuggle(football::sim::Tick now, const football::sim::PlayerTickContext& tick, const Vector3 &desiredDirection,
                                           float desiredVelocityFloat, std::span<MentalImage> history) {

  if (!enableMovementSmuggle) return Vector3(0);

  if (team->GetDesignatedTeamPossessionPlayer() != player || tick.designated_possession_player != player ||
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

    Line ballMovementLine;
    ballMovementLine.SetVertex(0, football::sim::observation::SampleMentalImage(history, mentalImageTime)->GetBallPrediction(0, now, tick.ball).Get2D());
    ballMovementLine.SetVertex(1, football::sim::observation::SampleMentalImage(history, mentalImageTime)->GetBallPrediction(futureTime_ms, now, tick.ball).Get2D());
    if (ballMovementLine.GetLength() < 0.5f) return Vector3(0); // ball is slow or very close

    float u = ballMovementLine.GetClosestToPoint(desiredBallPos);
    Vector3 closestBallPos = ballMovementLine.GetVertex(0) + (ballMovementLine.GetVertex(1) - ballMovementLine.GetVertex(0)) * u;

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
  maxPower += tick.ball.GetMovement().GetLength() * 0.5f; // can use some of current ballmomentum
  if (resultTouch.GetLength() > maxPower) {
    float missingPower = resultTouch.GetLength() - maxPower;
    resultTouch = resultTouch.GetNormalized(0) * maxPower;
    resultTouch.coords[2] += clamp(missingPower, 0.0f, 10.0f) * 0.25f;
  }

  // difficulty

  float difficultyFactor = GetCurrentBakedClip().metadata.touch_difficulty_factor;

  // apply stats
  if (functionType == e_FunctionType_ShortPass ||
      functionType == e_FunctionType_LongPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(football::model::PlayerStat::technical_shortpass) * 0.5f);
  if (functionType == e_FunctionType_HighPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(football::model::PlayerStat::technical_highpass)  * 0.5f);

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

  resultTouch.coords[2] += tick.ball.GetMovement().coords[2] * heightFactor * 0.5f +
                           heightFactor * 1.0f;

  resultTouch = resultTouch * (1.0f - ballMovementFactor) +
                tick.ball.GetMovement() * ballMovementFactor;

  resultTouch.coords[2] += difficultyFactor * 5.0f * rng_.Uniform(0.2f, 1.0f);

  return resultTouch;
}

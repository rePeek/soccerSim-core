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

#include "humanoid.hpp"

#include <cmath>

#include "../../../base/geometry/line.hpp"
#include "humanoid_utils.hpp"

#include "../player_retain_anchor.hpp"

#include "../player.hpp"
#include "../../team.hpp"
#include "../../match.hpp"

#include "../../../main.hpp"

#include "../../AIsupport/AIfunctions.hpp"

#include "../../../utils/animationextensions/footballanimationextension.hpp"


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

Humanoid::Humanoid(Player *player,
                   boost::shared_ptr<AnimCollection> animCollection)
    : HumanoidBase(player, player->GetTeam()->GetMatch(), animCollection) {
  DO_VALIDATION;
  team = CastPlayer()->GetTeam();
}

Humanoid::~Humanoid() { DO_VALIDATION; }

Player *Humanoid::CastPlayer() const { return static_cast<Player*>(player); }

bool _PassFiddlingEnabled() {
  DO_VALIDATION;
  return true;
}

void Humanoid::Process() {
  DO_VALIDATION;
  CastPlayer()->NoteProcessedPlayerTick();
  auto currentMentalImage = match->GetMentalImage(mentalImageTime);
  // this might be the solution to long-term imbalance
  decayingPositionOffset *= 0.95f;
  if (decayingPositionOffset.GetLength() < 0.005) decayingPositionOffset.Set(0);
  decayingDifficultyFactor = clamp(decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);

  assert(match);

  bool instaDoorheb = false;
  if (match->GetLastTouchTeamID() == team->GetID()) instaDoorheb = true;
  mentalImageTime = instaDoorheb ? 0 : CastPlayer()->GetController()->GetReactionTime_ms();

  // The authoritative movement state at tick start. Captured before
  // CalculateSpatialState so the procedural model integrates from the world
  // state in force, not from the animation prediction for this tick.
  const PlayerKinematicState tickStartState = player->GetKinematicState();
  // gate-mismatch-context: exactly one attribution per real Player tick, before
  // any of this tick's CalculateSpatialState/ProjectMovementState evaluations.
  // Gameplay continuity advances before the audit observes this tick, so the
  // epoch is decided by eligibility rather than by telemetry bookkeeping.
  CastPlayer()->AdvanceLocomotionContinuity(
      CastPlayer()->IsEligibleForProceduralLocomotion());
  // re-entry audit must see every real player tick, eligible or not, because
  // leaving locomotion is what arms the next re-entry classification.
  CastPlayer()->NoteLocomotionReentryTick(
      CastPlayer()->IsEligibleForProceduralLocomotion(),
      currentAnim.originatingCommand.useDesiredMovement,
      CastPlayer()->IsLocomotionIntentRefreshDue(
          static_cast<int>(match->GetActualTime_ms())),
      static_cast<int>(match->GetActualTime_ms()));
  if (CastPlayer()->IsEligibleForProceduralLocomotion()) {
    const PlayerCommand &simulation_command =
        CastPlayer()->GetSimulationMovementCommand();
    const bool legacy_gate = currentAnim.originatingCommand.useDesiredMovement;
    const bool simulation_gate = CastPlayer()->HasSimulationLocomotionIntent();
    // On the ticks the legacy gate already executes, does a decision-owned Direct
    // intent exist? And on the ticks only the simulation gate would execute, is
    // there a decision intent or just leftover compatibility payload?
    if (legacy_gate) {
      if (CastPlayer()->HasDecisionLocomotionIntent()) {
        ++DecisionLocomotionIntentPresentTicks();
      } else {
        ++DecisionLocomotionIntentMissingTicks();
        ++DecisionLocomotionIntentMissingForSource(
            static_cast<int>(CastPlayer()->GetSimulationMovementCommandSource()));
      }
    } else if (simulation_gate) {
      if (CastPlayer()->HasDecisionLocomotionIntent()) {
        ++DecisionLocomotionIntentPresentTicksLegacyGateFalse();
      } else {
        ++DecisionLocomotionIntentMissingTicksLegacyGateFalse();
        ++DecisionLocomotionIntentMissingForSourceLegacyGateFalse(
            static_cast<int>(CastPlayer()->GetSimulationMovementCommandSource()));
      }
    }
    if (CastPlayer()->GetLastDirectMovementIntentPublication_ms() >= 0) {
      const int decision_age_ms =
          static_cast<int>(match->GetActualTime_ms()) -
          CastPlayer()->GetLastDirectMovementIntentPublication_ms();
      DecisionLocomotionIntentAgeSum_ms() += decision_age_ms;
      ++DecisionLocomotionIntentAgeCount();
      if (decision_age_ms > DecisionLocomotionIntentAgeMax_ms()) {
        DecisionLocomotionIntentAgeMax_ms() = decision_age_ms;
      }
    }
    const int now_ms = static_cast<int>(match->GetActualTime_ms());
    RecordPlayerTickGateMismatch(
        legacy_gate, simulation_gate,
        CastPlayer()->IsSimulationMovementCommandInitialized(),
        static_cast<int>(CastPlayer()->GetSimulationMovementCommandSource()),
        simulation_command.desiredFunctionType,
        simulation_command.useDesiredMovement,
        CastPlayer()->GetLastDirectMovementIntentPublication_ms() < 0
            ? -1 : now_ms - CastPlayer()->GetLastDirectMovementIntentPublication_ms(),
        CastPlayer()->GetLastResetSituation_ms() < 0
            ? -1 : now_ms - CastPlayer()->GetLastResetSituation_ms());
    if (!legacy_gate && simulation_gate) {
      ++SimulationOnlyGateMismatchForResetContext(
          CastPlayer()->GetLastResetSituationAuditContext());
    }
  }

  // step 2: one controller query per tick, owned at tick scope, so a continuity
  // repair can run before this tick's locomotion execution and the later
  // selection phase reuses that same query instead of asking again.
  PlayerCommandQueue controllerQueue;
  bool controller_queried = false;
  bool controller_movement_published = false;
  const auto EnsureControllerQuery = [&]() {
    if (controller_queried) return;
    CastPlayer()->RequestCommand(controllerQueue);
    controller_queried = true;
    ++PlayerPathControllerQueries();
    bool has_movement = false;
    for (const PlayerCommand &controller_command : controllerQueue) {
      if (controller_command.desiredFunctionType == e_FunctionType_Movement &&
          controller_command.useDesiredMovement) {
        has_movement = true;
      }
    }
    if (has_movement) ++PlayerPathQueriesWithMovement();
    CastPlayer()->NoteControllerQuery(has_movement);
  };
  const auto AppendControllerCommandsToSelectionQueue =
      [&](PlayerCommandQueue &selectionQueue,
          std::vector<PlayerPathSelectionCommandProvenance> &provenance) {
        for (const PlayerCommand &controller_command : controllerQueue) {
          selectionQueue.push_back(controller_command);
          provenance.push_back(PlayerPathSelectionCommandProvenance::Controller);
        }
      };
  // Single publication point for the whole tick. The continuity repair and the
  // later selection phase both go through it, so a query is published at most
  // once and the publication counters stay consistent with the commits.
  const auto PublishControllerMovementOnce = [&]() {
    if (controller_movement_published) return false;
    if (CastPlayer()->PublishMovementIntentFromQueue(controllerQueue)) {
      controller_movement_published = true;
      ++PlayerPathDirectPublications();
      CastPlayer()->CommitLocomotionIntentRefresh();
      ++PlayerPathRefreshCommits();
      return true;
    }
    return false;
  };

  // step 2: continuity repair runs before this tick's locomotion execution. The
  // predicate is simulation eligibility plus a stale epoch, deliberately not the
  // animation gate: the decision state is repaired here while the legacy gate
  // still decides whether movement actually executes this tick.
  if (CastPlayer()->IsEligibleForProceduralLocomotion() &&
      CastPlayer()->DecisionLocomotionEpochIsStale()) {
    ++ContinuityRepairAttempts();
    EnsureControllerQuery();
    CastPlayer()->NoteDecisionPublicationCause(2);
    if (PublishControllerMovementOnce()) {
      ++ContinuityRepairPublications();
    } else {
      ++ContinuityRepairCandidatesMissing();
    }
    CastPlayer()->NoteDecisionPublicationCause(0);
  }

  CalculateSpatialState();
  spatialState.positionOffsetMovement = Vector3(0);
  // H3e1c-3c: pure locomotion ticks are produced by the simulation; every
  // other tick keeps the legacy root motion. Either way the legacy movement
  // fields end up as a projection of the authoritative kinematic state.
  ProjectMovementState(tickStartState);

  currentAnim.frameNum++;
  CastPlayer()->StepSimulationAction(10);
  const PlayerActionState &action =
      CastPlayer()->GetSimulationActionState();
  previousAnim_frameNum++;

  assert(team);
  int teamID = team->GetID();

  /*

    bump if (currentAnim.positionOffset.GetLength() > 0.1f && interruptAnim ==
    e_InterruptAnim_None) { DO_VALIDATION; interruptAnim = e_InterruptAnim_Bump;
    }
  */

  if (action.IsAtLastFrame() && interruptAnim == e_InterruptAnim_None) {
    DO_VALIDATION;
    interruptAnim = e_InterruptAnim_Switch;
  }

  bool mayReQueue = allowReQueue;

  // already some anim interrupt waiting?

  if (mayReQueue) {
    DO_VALIDATION;
    if (interruptAnim != e_InterruptAnim_None) {
      DO_VALIDATION;
      mayReQueue = false;
    }
  }

  // may requeue on this frame?

  /* can't do this: requeued movement anims should be requeueable into touch
    anims if (mayReQueue) { DO_VALIDATION; // never requeue a requeued anim if
    (currentAnim.originatingInterrupt != e_Interrupt_None &&
          currentAnim.originatingInterrupt != e_Interrupt_Switch) {
    DO_VALIDATION; mayRequeue = false;
    }*/

  if (mayReQueue) {
    DO_VALIDATION;
    bool frameNumPredicate = false;
    float actionDistance = ((spatialState.position + spatialState.movement * 0.1f) - match->GetBall()->Predict(100).Get2D()).GetLength();

    int team_id = team->GetID() == match->SecondTeam() ? 1 : 0;
    if (match->GetDesignatedPossessionPlayer() == player &&
        actionDistance < 3.0f) {
      DO_VALIDATION;
      frameNumPredicate = ((match->GetActualTime_ms() + team_id * 10) % 20) ==
                          0;  // .. 1 .. 2 .. 1 .. 2 ..

    } else if (match->GetDesignatedPossessionPlayer() == player) {
      DO_VALIDATION;
      frameNumPredicate = ((match->GetActualTime_ms() + team_id * 10) % 30) ==
                          0;  // .. 1 .. 2 .. x .. 1 .. 2 .. x ..

    } else if (team->GetDesignatedTeamPossessionPlayer() == player) {
      DO_VALIDATION;
      frameNumPredicate = ((match->GetActualTime_ms() + team_id * 20) % 40) ==
                          0;  // .. 1 .. x .. 2 .. x .. 1 .. x .. 2 ..

    } else if (actionDistance < 5.0f) {
      DO_VALIDATION;
      frameNumPredicate = ((match->GetActualTime_ms() + team_id * 20) % 50) ==
                          0;  // .. 1 .. x .. 2 .. x .. x ..

    } else if (actionDistance < 10.0f) {
      DO_VALIDATION;
      frameNumPredicate = ((match->GetActualTime_ms() + team_id * 40) % 80) ==
                          0;  // .. 1 .. x .. x .. x .. 2 .. x .. x .. x ..
    }

    if (!frameNumPredicate) mayReQueue = false;
  }

  // right anim to requeue?

  if (mayReQueue) {
    DO_VALIDATION;

    float ballDistance = (currentMentalImage->GetBallPrediction(500).Get2D() - spatialState.position).GetLength();
    if (((action.type == e_FunctionType_Movement &&
            !CastPlayer()->HasPossession() && ballDistance < 16.0f) ||
           (action.type == e_FunctionType_Movement &&
            CastPlayer()->HasPossession()) ||  // passes / shot
           (action.type == e_FunctionType_Trap && TouchPending()) ||
           (action.type == e_FunctionType_BallControl &&
            TouchPending())) &&
        /* now done later on, else we can't requeue to pass/shot during
        trap/ballcontrol (action.type == e_FunctionType_Trap &&
        TouchPending() && allowTrapReQueue && action.frame <=
        maxTrapReQueueFrame) || (action.type ==
        e_FunctionType_BallControl && TouchPending() && allowBallControlReQueue
        && action.frame <= maxBallControlReQueueFrame) ) &&
        */
        currentAnim.anim->GetVariableCache().incoming_special_state().empty() &&
        currentAnim.anim->GetVariableCache().outgoing_special_state().empty()) {
      DO_VALIDATION;
      mayReQueue = true;
    } else {
      mayReQueue = false;
    }
  }

  // okay, see if we need to requeue

  if (mayReQueue) {
    DO_VALIDATION;
    interruptAnim = e_InterruptAnim_ReQueue;
  }

  const bool legacy_opportunity = interruptAnim != e_InterruptAnim_None;
  const bool simulation_due =
      CastPlayer()->NoteLocomotionIntentCadence(legacy_opportunity);
  // Tag the cause of any publication on this path, so a legacy-driven query is
  // not credited to the simulation cadence in the audit.
  CastPlayer()->NoteDecisionPublicationCause(
      legacy_opportunity && !simulation_due ? 1 : 0);
  if (legacy_opportunity || simulation_due) {
    DO_VALIDATION;
    PlayerCommandQueue commandQueue;      // selection queue
    std::vector<PlayerPathSelectionCommandProvenance> commandProvenance;

    if (interruptAnim == e_InterruptAnim_Trip && tripType != 0) {
      DO_VALIDATION;
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
      EnsureControllerQuery();
      AppendControllerCommandsToSelectionQueue(commandQueue, commandProvenance);
    }

    // iterate through the command queue and pick the first that is applicable

    bool found = false;
    bool preferPassAndShot = false; // pass/shot and such; in that case we want trap/ballcontrol anims to be less prefered
    for (unsigned int i = 0; i < commandQueue.size(); i++) {
      DO_VALIDATION;

      const PlayerCommand &command = commandQueue[i];
      const PlayerPathSelectionCommandProvenance provenance =
          commandProvenance[i];
      const int materialReanchorsBefore =
          provenance == PlayerPathSelectionCommandProvenance::LocalTripMovementFallback
              ? MovementCommandReanchorsMateriallyDifferent()
              : 0;

      if (command.desiredFunctionType == e_FunctionType_ShortPass ||
          command.desiredFunctionType == e_FunctionType_LongPass ||
          command.desiredFunctionType == e_FunctionType_HighPass ||
          command.desiredFunctionType == e_FunctionType_Shot) {
        DO_VALIDATION;
        preferPassAndShot = true;
      }
      found = SelectAnim(command, interruptAnim, preferPassAndShot);
      if (found) {
        if (provenance == PlayerPathSelectionCommandProvenance::LocalTrip) {
          ++PlayerPathLocalTripSelected();
        } else if (provenance ==
                   PlayerPathSelectionCommandProvenance::LocalTripMovementFallback) {
          ++PlayerPathLocalTripMovementFallbackSelected();
          if (MovementCommandReanchorsMateriallyDifferent() >
              materialReanchorsBefore) {
            ++PlayerPathLocalTripMovementFallbackMaterialReanchor();
          }
          // SelectAnim() has returned, including its temporary raw re-anchor.
          // This local fallback is simulation-owned but not controller-owned, so
          // it becomes the final writer without consuming the refresh scheduler.
          CastPlayer()->SetSimulationMovementCommand(
              command, LocomotionCommandSource::SimulationFallbackIntent);
        }
        break;
      }
    }

    if (interruptAnim == e_InterruptAnim_Switch && !found) {
      DO_VALIDATION;
      Log(e_Warning, "Humanoid", "Process", "RED ALERT! NO APPLICABLE ANIM FOUND! NOOOO!");
      Log(e_Warning, "Humanoid", "Process", "currentanimtype: " + currentAnim.anim->GetVariable("type"));
      for (unsigned int i = 0; i < commandQueue.size(); i++) {
        DO_VALIDATION;
        Log(e_Warning, "Humanoid", "Process", "desiredanimtype:" + int_to_str(commandQueue[i].desiredFunctionType));
        Log(e_Warning, "Humanoid", "Process", "desired velo: " + real_to_str(commandQueue[i].desiredVelocityFloat));
        Log(e_Warning, "Humanoid", "Process", "desired direction: " + real_to_str(commandQueue[i].desiredDirection.coords[0]) + ", " + real_to_str(commandQueue[i].desiredDirection.coords[1]) + ", " + real_to_str(commandQueue[i].desiredDirection.coords[2]));
      }
      Log(e_Warning, "Humanoid", "Process", "current velo: " + real_to_str(spatialState.floatVelocity));
      Log(e_Warning, "Humanoid", "Process", "current body angle: abs: " + real_to_str(spatialState.bodyAngle) + ", rel: " + real_to_str(spatialState.relBodyAngle));
      Log(e_Warning, "Humanoid", "Process", "special state: " + currentAnim.anim->GetVariableCache().outgoing_special_state());
      print_stacktrace();
      exit(1);
    }


    // 4b''-player-path: the single publication point, after SelectAnim has fully
    // returned, so the legacy re-anchor inside it cannot remain the final writer.
    // Publication reads only the controller queue, so the trip fallback above can
    // never be published as a DirectMovementIntent.
    // One publication per controller query: a pre-execution continuity repair may
    // already have published from this same query, and it must not be repeated.
    if (controller_queried && !controller_movement_published) {
      // The publication counters live in the lambda, so the repair and this site
      // cannot double count.
      if (!PublishControllerMovementOnce()) {
        ++PlayerPathCandidatesMissing();
      }
    }
    if (found) {
      DO_VALIDATION;
      startPos = spatialState.position;
      startAngle = spatialState.angle;

      CalculatePredictedSituation(nextStartPos, nextStartAngle);

      // decaying difficulty
      float animDiff = atof(currentAnim.anim->GetVariable("animdifficultyfactor").c_str());
      if (animDiff > decayingDifficultyFactor) decayingDifficultyFactor = animDiff;

      // if we just requeued, for example, from movement to ballcontrol, there's no reason we can not immediately requeue to another ballcontrol again (next time). only apply the initial requeue delay on subsequent anims of the same type
      // (so we can have a fast ballcontrol -> ballcontrol requeue, but after that, use the initial delay)
      if (interruptAnim == e_InterruptAnim_ReQueue &&
          previousAnim_functionType == action.type) {
        DO_VALIDATION;
        reQueueDelayFrames = initialReQueueDelayFrames; // don't try requeueing (some types of anims, see selectanim()) too often
      }
    }
  }
  reQueueDelayFrames = std::max(reQueueDelayFrames - 1, 0);

  interruptAnim = e_InterruptAnim_None;

  if (startPos.coords[2] != 0.f) {
    DO_VALIDATION;
    Log(e_FatalError, "Humanoid", "Process", "BWAAAAAH FLYING PLAYERS!! height: " + real_to_str(startPos.coords[2]));
  }

  float ballDistanceNow = (match->GetBall()->Predict(0).Get2D() - spatialState.position).GetLength();
  float ballDistanceFuture = (match->GetBall()->Predict(200).Get2D() - (spatialState.position + spatialState.movement * 0.2f)).GetLength();
  float lastTouchBias = CastPlayer()->GetLastTouchBias(1500);
  float oppLastTouchBias = match->GetTeam(std::abs(team->GetID() - 1))->GetLastTouchBias(240);

  if (CastPlayer() == match->GetDesignatedPossessionPlayer() &&
      ((lastTouchBias <= 0.01f && oppLastTouchBias <= 0.01f &&
        action.type == e_FunctionType_Movement &&
        ballDistanceNow < 0.6f && ballDistanceFuture > 0.65f &&
        ballDistanceFuture > ballDistanceNow)  // 0.5 / 0.6

       ||

       (lastTouchBias <= 0.7f && CastPlayer()->HasPossession() &&
        action.type == e_FunctionType_Trip &&
        ballDistanceNow < 0.4f)

       ) &&
      match->GetBall()->Predict(0).coords[2] < 1.6f) {
    DO_VALIDATION;

    CastPlayer()->TriggerControlledBallCollision();
    //SetGreenDebugPilon(spatialState.position);
  }

  // ------------------------ EXPERIMENTAL ------------------------------------------------
  bool controlledBallCollision = CastPlayer()->IsControlledBallCollisionTriggered();
  if (controlledBallCollision) CastPlayer()->ResetControlledBallCollisionTrigger();
  if (controlledBallCollision && !action.HasScheduledContact()) {
    DO_VALIDATION;
    Vector3 currentBallVec = match->GetBall()->GetMovement();
    radian nextBodyAngle = startAngle + currentAnim.anim->GetOutgoingAngle() + currentAnim.anim->GetOutgoingBodyAngle() + currentAnim.rotationSmuggle.end;

    radian xRot = 0;
    radian yRot = 0;
    Vector3 touchVec = GetTrapVector(match, CastPlayer(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
    if (currentAnim.originatingCommand.modifier &
        e_PlayerCommandModifier_KnockOn) {
      DO_VALIDATION;
      touchVec *= 1.35f;//1.2f;
    }

    float bumpyRideBias = 0.0f;
    touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

    match->GetBall()->Touch(touchVec);
    match->GetBall()->SetRotation(xRot, yRot, 0, 0.2f * (1.0f - bumpyRideBias)); // 0.9
    team->SetLastTouchPlayer(CastPlayer(), GetTouchTypeForBodyPart(currentAnim.anim->GetVariable("touch_bodypart")));//, e_TouchType_Accidental);
  }
  // ---------------------- / EXPERIMENTAL ------------------------------------------------

  if (action.HasScheduledContact() &&
      action.frame == action.contactFrame) {
    DO_VALIDATION;

    Vector3 desiredBallPosition;
    boost::static_pointer_cast<FootballAnimationExtension>(currentAnim.anim->GetExtension("football"))->GetTouchPos(action.contactFrame, desiredBallPosition);
    float desiredBallHeight = desiredBallPosition.coords[2];

    float touchableDistance = 0.4f;

    float fullBallDistance = (match->GetBall()->Predict(0) - (currentAnim.touchPos + currentAnim.positionOffset)).GetLength();

    if (!currentAnim.anim->GetVariableCache().incoming_retain_state().empty()) {
      DO_VALIDATION;
      fullBallDistance = 0.0f;
      touchableDistance = 1.0f;
    }

    float bumpyRideBias = fullBallDistance / touchableDistance;
    bumpyRideBias = clamp(bumpyRideBias - 0.001f, 0.0f, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 1.0f);
    bumpyRideBias = curve(bumpyRideBias, 0.5f);
    Vector3 currentBallVec = match->GetBall()->GetMovement();

    if (fullBallDistance < touchableDistance &&
        std::fabs(desiredBallHeight - match->GetBall()->Predict(0).coords[2]) <
            1.0f) {
      DO_VALIDATION;

      radian nextBodyAngle = startAngle + currentAnim.anim->GetOutgoingAngle() + currentAnim.anim->GetOutgoingBodyAngle() + currentAnim.rotationSmuggle.end;

      if (currentAnim.functionType == e_FunctionType_Trap ||
          (currentAnim.functionType == e_FunctionType_BallControl &&
           CastPlayer()->HasPossession() == false)) {
        DO_VALIDATION;
        //printf("trap!\n");
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(match, CastPlayer(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        if (currentAnim.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          DO_VALIDATION;
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);
        match->GetBall()->SetRotation(xRot, yRot, 0, 0.5f * (1.0f - bumpyRideBias));

        team->SetLastTouchPlayer(CastPlayer(), GetTouchTypeForBodyPart(currentAnim.anim->GetVariable("touch_bodypart")));
      }

      else if (currentAnim.functionType == e_FunctionType_BallControl) {
        DO_VALIDATION;
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetBallControlVector(match->GetBall(), CastPlayer(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        if (currentAnim.originatingCommand.modifier &
            e_PlayerCommandModifier_KnockOn) {
          DO_VALIDATION;
          touchVec *= 1.35f;
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);
        match->GetBall()->SetRotation(xRot, yRot, 0, 0.6f * (1.0f - bumpyRideBias)); // 1.0

        team->SetLastTouchPlayer(CastPlayer(), GetTouchTypeForBodyPart(currentAnim.anim->GetVariable("touch_bodypart")));
      }

      else if (currentAnim.functionType == e_FunctionType_ShortPass ||
               currentAnim.functionType == e_FunctionType_LongPass ||
               currentAnim.functionType == e_FunctionType_HighPass) {
        DO_VALIDATION;

        Vector3 ballDirection = currentAnim.originatingCommand.touchInfo.desiredDirection;
        float ballPower = currentAnim.originatingCommand.touchInfo.desiredPower;
        Player *targetPlayer = currentAnim.originatingCommand.touchInfo.targetPlayer;
        Vector3 inputDirection = currentAnim.originatingCommand.touchInfo.inputDirection;
        if (CastPlayer()->ExternalControllerActive()) inputDirection = CastPlayer()->ExternalController()->GetDirection();

        // refine/change target, if new target is close enough to old target

        //targetPlayer = 0;//currentAnim.originatingCommand.touchInfo.targetPlayer;
        Vector3 tmpBallDirection = ballDirection;
        float tmpBallPower = ballPower;
        Player *tmpTargetPlayer = 0;
        Player *forcedTargetPlayer = 0;
        AI_GetPass(CastPlayer(), currentAnim.originatingCommand.desiredFunctionType, inputDirection, currentAnim.originatingCommand.touchInfo.inputPower, currentAnim.originatingCommand.touchInfo.autoDirectionBias, currentAnim.originatingCommand.touchInfo.autoPowerBias, tmpBallDirection, tmpBallPower, tmpTargetPlayer, currentAnim.originatingCommand.touchInfo.forcedTargetPlayer);
        float maxDeviationAngle = 0.15f * pi;
        radian angleDiff = tmpBallDirection.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) <= maxDeviationAngle) {
          DO_VALIDATION;
          ballDirection = tmpBallDirection;
          ballPower = tmpBallPower;
          targetPlayer = tmpTargetPlayer;
        } else if (std::fabs(angleDiff) < 2.0f * maxDeviationAngle) {
          DO_VALIDATION;
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);

          if (tmpTargetPlayer != targetPlayer) {
            DO_VALIDATION;
            // if we can't make it to our refined target at all, just stick with original ballpower (think about refined target at ~180 deg, would be weird to pass forward with the power of that (unreachable) target)
            float refinedBias = NormalizedClamp(std::fabs(clampedAngleDiff), 0.0f, std::fabs(angleDiff));
            ballPower = ballPower * (1.0f - refinedBias) + tmpBallPower * refinedBias;
            targetPlayer = tmpTargetPlayer; // new, and removed line below
          } else {
            ballPower = tmpBallPower;
          }

        }  // else: just stick to original

        if (targetPlayer) {
          team->SelectPlayer(targetPlayer);

        }
        float zcurve = 0.0f;
        Vector3 touchVec = ballDirection * 36 * (ballPower + 0.3f);

        if (_PassFiddlingEnabled()) {
          DO_VALIDATION;
          //SetGreenDebugPilon(match->GetBall()->Predict(0).Get2D() + touchVec.Get2D() * 0.4f);

          touchVec = GetBestPossibleTouch(touchVec, currentAnim.functionType);

          // add a little curve for aesthetics & realism
          radian bodyTouchAngle = spatialState.bodyDirectionVec.GetAngle2D(touchVec) / pi;
          if (std::fabs(bodyTouchAngle) > 0.5f) bodyTouchAngle = (1.0f - std::fabs(bodyTouchAngle)) * signSide(bodyTouchAngle);
          bodyTouchAngle *= 2.0f;
          //printf("bodyTouchAngle: %f\n", bodyTouchAngle);
          radian amount = bodyTouchAngle * 0.25f;
          if (currentAnim.functionType == e_FunctionType_HighPass) amount *= 0.2f;
          touchVec.Rotate2D(amount * (0.4f + 0.6f * NormalizedClamp(touchVec.GetLength(), 0.0f, 70.0f)));
          zcurve = amount * -340;//-600;

          //SetRedDebugPilon(match->GetBall()->Predict(0).Get2D() + touchVec.Get2D() * 0.4f);
        }

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);
        float forwardness = 3.5f;
        if (currentAnim.functionType == e_FunctionType_HighPass) forwardness = -1.3f;
        radian xRot = touchVec.GetNormalized(0).coords[1] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        radian yRot = touchVec.GetNormalized(0).coords[0] * (clamp(touchVec.GetLength(), 0.0, 15.0) * forwardness);
        match->GetBall()->SetRotation(xRot, yRot, zcurve, 0.9f * (1.0f - bumpyRideBias));

        team->SetLastTouchPlayer(CastPlayer(), GetTouchTypeForBodyPart(currentAnim.anim->GetVariable("touch_bodypart")));
      }

      else if (currentAnim.functionType == e_FunctionType_Shot) {
        DO_VALIDATION;

        // alter direction, if needed
        Vector3 ballDirection = currentAnim.originatingCommand.touchInfo.desiredDirection;
        Vector3 inputDirection = currentAnim.originatingCommand.touchInfo.inputDirection;
        if (CastPlayer()->ExternalControllerActive()) inputDirection = CastPlayer()->ExternalController()->GetDirection();
        Vector3 ballDirectionAltered = AI_GetShotDirection(CastPlayer(), inputDirection, currentAnim.originatingCommand.touchInfo.autoDirectionBias);

        float maxDeviationAngle = 0.1f * pi;
        radian angleDiff = ballDirectionAltered.Get2D().GetAngle2D(ballDirection.Get2D());
        if (std::fabs(angleDiff) > maxDeviationAngle) {
          DO_VALIDATION;
          // get as close as possible
          float clampedAngleDiff = clamp(angleDiff, -maxDeviationAngle, maxDeviationAngle);
          ballDirection = ballDirection.GetRotated2D(clampedAngleDiff);
        } else {
          ballDirection = ballDirectionAltered;
        }

        radian xRot = 0;
        radian yRot = 0;
        radian zRot = 0;
        Vector3 touchVec = GetShotVector(match, CastPlayer(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot, zRot, currentAnim.originatingCommand.touchInfo.autoDirectionBias);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);
        match->GetBall()->SetRotation(xRot, yRot, zRot, 0.7f * (1.0f - bumpyRideBias));
        team->SetLastTouchPlayer(CastPlayer(), GetTouchTypeForBodyPart(currentAnim.anim->GetVariable("touch_bodypart")));
      }

      else if (currentAnim.functionType == e_FunctionType_Interfere) {
        DO_VALIDATION;
        radian xRot = 0;
        radian yRot = 0;
        Vector3 touchVec = GetTrapVector(match, CastPlayer(), nextStartPos, nextStartAngle, nextBodyAngle, CalculateOutgoingMovement(currentAnim.positions), currentAnim, currentAnim.frameNum, spatialState, decayingPositionOffset, xRot, yRot);
        touchVec =
            touchVec * 0.5f +
            (match->GetBall()->Predict(0).Get2D() - spatialState.position)
                    .GetNormalized() *
                4.0f +
            Vector3(0, 0, boostrandom(0.5f, 1.5f));  // was 1 .. 6

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);
        match->GetBall()->SetRotation(xRot, yRot, 0.3f * (1.0f - bumpyRideBias));
        team->SetLastTouchPlayer(CastPlayer(), e_TouchType_Accidental); // it's not truly accidental, but the resulting direction somewhat is, so goalies may fetch these balls
      }

      else if (currentAnim.functionType == e_FunctionType_Deflect) {
        DO_VALIDATION;
        bool canRetain = true; // can we grab hold of the ball?
        if (currentAnim.anim->GetVariable("outgoing_retain_state").compare("") == 0) canRetain = false; // not the right anim, hopeless!
        if (match->GetBallRetainer() != 0) canRetain = false; // somebody is already holding the ball :( (dafuq, this should not happen, right?)

        float veloDifficulty = NormalizedClamp((match->GetBall()->GetMovement() - spatialState.movement).GetLength(), 0.0f, 40.0f);
        float reactionDifficulty = 0.0f;
        Player *lastTouchPlayer = match->GetTeam(std::abs(team->GetID() - 1))->GetLastTouchPlayer();
        if (lastTouchPlayer) {
          DO_VALIDATION;
          reactionDifficulty =
              std::pow(lastTouchPlayer->GetLastTouchBias(
                           1200 - player->GetStat(physical_reaction) * 400),
                       0.6f);
        }
        if ((1.0f - veloDifficulty) * (1.0f - reactionDifficulty) < 0.3f) canRetain = false; // too hard!

        if (canRetain) {
          DO_VALIDATION;
          match->SetBallRetainer(CastPlayer());
        } else {
          Vector3 currentBallMovement = match->GetBall()->GetMovement().Get2D();
          Vector3 playerMovement = spatialState.movement;
          Vector3 touchVec =
              (-currentBallMovement * 0.1f + playerMovement * 2.0f +
               Vector3(-team->GetDynamicSide(), 0, 0) * 4.0f +
               Vector3(0, boostrandom(-1, 1), 0))
                  .GetNormalized(0) *
              (currentBallMovement.GetLength() * 0.3f +
               playerMovement.GetLength() * 2.5f);
          touchVec.coords[2] += 1.2f;

          touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

          match->GetBall()->Touch(touchVec);
          match->GetBall()->SetRotation(0, 0, 0, 0.2f * (1.0f - bumpyRideBias));
        }
        team->SetLastTouchPlayer(CastPlayer(), e_TouchType_Accidental);
      }

      else if (currentAnim.functionType == e_FunctionType_Sliding) {
        DO_VALIDATION;
        Vector3 touchVec = GetVectorFromString(currentAnim.anim->GetVariable("balldirection")).GetRotated2D(spatialState.angle);
        touchVec = touchVec * 6.0f + match->GetBall()->GetMovement() * -0.28f;
        touchVec += Vector3(0, 0, 6);

        touchVec = touchVec * (1.0f - bumpyRideBias) + currentBallVec * bumpyRideBias;

        match->GetBall()->Touch(touchVec);

        team->SetLastTouchPlayer(CastPlayer(), e_TouchType_Accidental);
      }
    }
  }

  if (match->GetBallRetainer() == player) {
    DO_VALIDATION;
    if (((!action.HasScheduledContact() ||
          action.frame >= action.contactFrame) &&
         currentAnim.anim->GetVariable("outgoing_retain_state") != "") ||
        (action.HasScheduledContact() &&
         action.frame < action.contactFrame &&
         currentAnim.anim->GetVariableCache().incoming_retain_state() != "") ||
        (currentAnim.anim->GetVariable("incoming_retain_state") != "" &&
         currentAnim.anim->GetVariable("outgoing_retain_state") != "")) {
      DO_VALIDATION;
      // Anchor the ball to a body-semantic local offset. The retain state
      // string decides which anchor; the position itself is a pure function of
      // simulation state, so it no longer depends on when the animation pose
      // was last refreshed.
      std::string retainState =
          currentAnim.anim->GetVariable("outgoing_retain_state");
      if (retainState.empty()) {
        DO_VALIDATION;
        retainState = currentAnim.anim->GetVariable("incoming_retain_state");
      }
      RetainAnchorKind anchor;
      if (!ParseRetainAnchorKind(retainState, anchor)) {
        DO_VALIDATION;
        // Fail fast: a silent fallback would place the retained ball
        // somewhere plausible but wrong.
        Log(e_FatalError, "Humanoid", "Process",
            "unknown retain state: " + retainState);
        exit(1);
      }
      match->GetBall()->Touch(Vector3(0));
      match->GetBall()->SetRotation(0, 0, 0, 1.0);
      match->GetBall()->SetPosition(ComputeRetainAnchor(
          spatialState.position, spatialState.bodyDirectionVec, anchor));
      team->SetLastTouchPlayer(CastPlayer(), e_TouchType_Intentional_Nonkicked);
    } else {
      // no longer retaining
      match->SetBallRetainer(0);
    }
  }

  // action smuggle

  // start with +1, because we want to influence the first frame as well
  // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
  // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
  float frameBias = (currentAnim.frameNum + 1) / (float)(currentAnim.anim->GetEffectiveFrameCount() + 1);

  if (currentAnim.touchFrame != -1 &&
      currentAnim.frameNum <= currentAnim.touchFrame) {
    DO_VALIDATION;
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
      currentAnim.frameNum <= currentAnim.anim->GetEffectiveFrameCount()) {
    DO_VALIDATION;  // omit one frame, or balltouch will be influenced because
                    // of velo
    // linear version *outdated*
    // spatialState.movementSmuggleMovement = currentAnim.movementSmuggle / (float)(currentAnim.anim->GetEffectiveFrameCount());
    // currentAnim.movementSmuggleOffset += spatialState.movementSmuggleMovement;

    // smooth version
    float value =
        std::cos((currentAnim.frameNum /
                      (float)(currentAnim.anim->GetEffectiveFrameCount() + 1) -
                  0.5f) *
                 pi * 2.0f) +
        1.0f;
    // add some linearity
    value = value * 0.1f + 0.9f;
    spatialState.movementSmuggleMovement = (currentAnim.movementSmuggle / (float)(currentAnim.anim->GetEffectiveFrameCount() + 1)) * value * 100.0f;
    currentAnim.movementSmuggleOffset += spatialState.movementSmuggleMovement / 100.0f;
  } else {
    spatialState.movementSmuggleMovement = Vector3(0);
  }

  // rotation smuggle

  int beginRotationFrameCount = 16; // after this amount of frames, be ready with 'ease-in' rotation smuggle
  float cappedFrameBias = std::min(1.0f, (currentAnim.frameNum + 1) / (float)std::min(beginRotationFrameCount, currentAnim.anim->GetEffectiveFrameCount() + 1));
  float beginFrameBias = cappedFrameBias;
  float endFrameBias = cappedFrameBias;
  if (currentAnim.touchFrame != -1) {
    DO_VALIDATION;
    // beginFrameBias ranges from 0 to 1 during frame 0 to (touchframe OR beginRotationFrameCount) (depending on which comes first)
    beginFrameBias = std::min(1.0f, (currentAnim.frameNum + 1) / (float)std::min(beginRotationFrameCount, currentAnim.touchFrame + 1));
    if (!allowPreTouchRotationSmuggle) {
      DO_VALIDATION;
      if (currentAnim.frameNum > currentAnim.touchFrame) {
        DO_VALIDATION;
        // end rotation smuggle starts after touch
        endFrameBias = (currentAnim.frameNum - currentAnim.touchFrame) / (float)(currentAnim.anim->GetEffectiveFrameCount() - currentAnim.touchFrame);
      } else {
        // no smuggle before touch
        endFrameBias = 0.0f;
      }
    }
  }
  currentAnim.rotationSmuggleOffset = currentAnim.rotationSmuggle.begin * (1.0f - beginFrameBias) +
                                       currentAnim.rotationSmuggle.end   * endFrameBias;

  // ballretainer should not get out of 16 meter box

  if (match->GetBallRetainer() == player &&
      CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK &&
      (match->IsInSetPiece() == false && match->IsInPlay() == true)) {
    DO_VALIDATION;
    if (match->GetBall()->Predict(0).coords[1] > 20.05f) {
      DO_VALIDATION;
      OffsetPosition(Vector3(0, clamp(20.05f - match->GetBall()->Predict(0).coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (match->GetBall()->Predict(0).coords[1] < -20.05f) {
      DO_VALIDATION;
      OffsetPosition(Vector3(0, clamp(-20.05f - match->GetBall()->Predict(0).coords[1], -0.5f, 0.5f), 0) * 0.3f);
    }
    if (match->GetBall()->Predict(0).coords[0] * -team->GetDynamicSide() >
        -pitchHalfW + 16.4f) {
      DO_VALIDATION;
      OffsetPosition(Vector3(clamp((-pitchHalfW + 16.4f) -
                                       match->GetBall()->Predict(0).coords[0] *
                                           -team->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -team->GetDynamicSide() * 0.3f);
    }
    if (match->GetBall()->Predict(0).coords[0] * -team->GetDynamicSide() <
        -pitchHalfW + 0.1f) {
      DO_VALIDATION;
      OffsetPosition(Vector3(clamp((-pitchHalfW + 0.1f) -
                                       match->GetBall()->Predict(0).coords[0] *
                                           -team->GetDynamicSide(),
                                   -0.5f, 0.5f),
                             0, 0) *
                     -team->GetDynamicSide() * 0.4f);
    }
  }

  // next frame

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    DO_VALIDATION;
    //printf("size: %i\n", currentAnim.positions.size());
  } else {
  }
}

void Humanoid::SelectRetainAnim() {
  DO_VALIDATION;
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;
  query.properties.set("incoming_retain_state", "right_elbow");
  query.properties.set("outgoing_retain_state", "right_elbow");

  DataSet dataSet;
  anims->CrudeSelection(dataSet, query);

  assert(dataSet.size() != 0);

  GetContext().tracker_disabled++;
  std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareMovementSimilarity, this, _1, _2));
  GetContext().tracker_disabled--;

  startAngle = FixAngle((Vector3(0) - startPos).GetAngle2D());//0.5 * pi; (facing right)

  currentAnim.positions.clear();
  currentAnim.anim = anims->GetAnim(*dataSet.begin());
  currentAnim.id = *dataSet.begin();
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

  match->SetBallRetainer(CastPlayer());
}

void Humanoid::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  HumanoidBase::ResetSituation(focusPos);
  //printf("humanoid reset\n");
}

bool Humanoid::SelectAnim(const PlayerCommand &command,
                          e_InterruptAnim localInterruptAnim,
                          bool preferPassAndShot) {
  DO_VALIDATION;  // returns false on no applicable anim found
  assert(command.desiredDirection.coords[2] == 0.0f);
  const PlayerActionState &action =
      CastPlayer()->GetSimulationActionState();
  const bool material_candidate =
      RecordSchedulerQuery(currentAnim.originatingCommand, command);

  // optimizations
  auto currentMentalImage = match->GetMentalImage(mentalImageTime);
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding) {
    DO_VALIDATION;
    if ((currentMentalImage->GetBallPrediction(200).Get2D() -
         spatialState.position)
            .GetLength() > ballDistanceOptimizeThreshold) {
      DO_VALIDATION;
      return false;
    }
    if ((currentMentalImage->GetBallPrediction(defaultTouchOffset_ms).Get2D() -
         spatialState.position)
                .GetLength() > 2.0f &&
        //    match->GetBall()->GetMovement().GetNormalized(0).GetDotProduct(player->GetMovement().GetNormalizedMax(1.0f))
        //    < 0) { DO_VALIDATION; // ball and player going the other way
        (currentMentalImage->GetBallPrediction(defaultTouchOffset_ms).Get2D() -
         (spatialState.position +
          spatialState.movement * defaultTouchOffset_ms * 0.001))
                .GetLength() >
            (currentMentalImage->GetBallPrediction(0).Get2D() -
             (spatialState.position))
                .GetLength()) {
      DO_VALIDATION;  // ball moving away from player
      return false;
    }
  }

  // this stops these anims from happening when opp has touched the ball, deflecting the ball too far away
  if (command.desiredFunctionType != e_FunctionType_Movement &&
      command.desiredFunctionType != e_FunctionType_Trip &&
      command.desiredFunctionType != e_FunctionType_Special &&
      command.desiredFunctionType != e_FunctionType_Sliding &&
      command.desiredFunctionType != e_FunctionType_Deflect &&
      match->GetBallRetainer() != player) {
    DO_VALIDATION;
    if ((currentMentalImage->GetBallPrediction(1000) - match->GetBall()->Predict(1000)).GetLength() > 2.0f) return false;
  }

  // /optimizations

  if (localInterruptAnim == e_InterruptAnim_ReQueue) {
    DO_VALIDATION;

    float focusDistance = (match->GetDesignatedPossessionPlayer()->GetPosition() - spatialState.position).GetLength();

    if (action.type != e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        (CastPlayer()->HasPossession() || focusDistance > 12.0f)) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        action.frame + minRemainingMovementReQueueFrames >
            action.frameCount - 1) return false;
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_Movement &&
        (!allowMovementReQueue || reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_BallControl &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (!allowBallControlReQueue ||
         action.frame > maxBallControlReQueueFrame ||
         reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_BallControl &&
        command.desiredFunctionType == e_FunctionType_Trap) return false;
    if (action.type == e_FunctionType_Trap &&
        command.desiredFunctionType == e_FunctionType_Trap &&
        (!allowTrapReQueue ||
         action.frame + minRemainingTrapReQueueFrames > action.contactFrame ||
         reQueueDelayFrames > 0)) return false;
    if (action.type == e_FunctionType_Trap &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (!allowTrapReQueue ||
         action.frame + minRemainingTrapReQueueFrames > action.contactFrame ||
         reQueueDelayFrames > 0)) return false;

    // too similar to what we are already trying to accomplish
    if (currentAnim.originatingCommand.desiredFunctionType ==
            command.desiredFunctionType &&
        ((currentAnim.originatingCommand.desiredDirection *
          currentAnim.originatingCommand.desiredVelocityFloat) -
         (command.desiredDirection * command.desiredVelocityFloat))
                .GetLength() < 1.5f) {
      DO_VALIDATION;
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
      DO_VALIDATION;

      // current change in momentum
      Vector3 plannedMomentumChange = currentAnim.outgoingMovement - currentAnim.incomingMovement;
      Vector3 desiredMomentumChange = (command.desiredDirection * command.desiredVelocityFloat) - spatialState.movement;

      if ((desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.0f &&
           desiredMomentumChange.GetDistance(plannedMomentumChange) < 4.0f) ||
          desiredMomentumChange.GetDotProduct(plannedMomentumChange) > 0.8f ||
          desiredMomentumChange.GetDistance(plannedMomentumChange) < 2.0f) {
        DO_VALIDATION;
        return false;
      }
    }

    // don't requeue movement to ballcontrol halfway movement anims, unless there's a serious change of movement desired
    if (action.type == e_FunctionType_Movement &&
        command.desiredFunctionType == e_FunctionType_BallControl &&
        (match->GetActualTime_ms() - CastPlayer()->GetLastTouchTime_ms() <
             600 &&
         CastPlayer()->GetLastTouchType() == e_TouchType_Intentional_Kicked) &&
        CastPlayer()->HasPossession()) {
      DO_VALIDATION;  // && !CastPlayer()->AllowLastDitch()) { DO_VALIDATION;
      float desiredMovementChange = (spatialState.movement - (command.desiredDirection * command.desiredVelocityFloat)).GetLength();
      if (desiredMovementChange < 1.0f) return false;
    }
  }

  if (localInterruptAnim != e_InterruptAnim_ReQueue || action.frame > 12)
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
    DO_VALIDATION;
    query.byOutgoingBallDirection = true;
    query.outgoingBallDirection = command.touchInfo.desiredDirection.GetRotated2D(-spatialState.angle);
  }

  query.byIncomingVelocity = true;
  query.incomingVelocity = spatialState.enumVelocity;

  if (query.functionType != e_FunctionType_Movement && query.incomingVelocity == e_Velocity_Dribble) query.incomingVelocity = e_Velocity_Walk;

  query.incomingVelocity_Strict = false;
  if (query.functionType != e_FunctionType_Movement &&
      query.functionType != e_FunctionType_BallControl) {
    DO_VALIDATION;
    query.incomingVelocity_ForceLinearity = false;
    if (query.functionType != e_FunctionType_Deflect) {
      DO_VALIDATION;
      query.incomingVelocity_NoDribbleToSprint = true;
      if (query.functionType != e_FunctionType_ShortPass &&
          query.functionType != e_FunctionType_LongPass &&
          query.functionType != e_FunctionType_HighPass &&
          query.functionType != e_FunctionType_Shot) {
        DO_VALIDATION;
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
    DO_VALIDATION;
    query.incomingBodyDirection_Strict = false;
    if (query.functionType != e_FunctionType_Deflect) {
      DO_VALIDATION;
      if (query.functionType != e_FunctionType_BallControl) {
        DO_VALIDATION;
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
      currentAnim.anim->GetVariableCache().outgoing_special_state().empty() &&
      match->GetBallRetainer() != CastPlayer()) {
    DO_VALIDATION;
    Vector3 playerLookAtVec = (command.desiredLookAt - spatialState.position).GetNormalized(spatialState.directionVec);
    query.lookAtVecRel = playerLookAtVec.GetRotated2D(-spatialState.angle);
    query.bySide = true;
  }

  if (command.onlyDeflectAnimsThatPickupBall == true) {
    DO_VALIDATION;
    query.byPickupBall = true;
    query.pickupBall = true;
  }

  if (command.desiredFunctionType == e_FunctionType_Trap ||
      command.desiredFunctionType == e_FunctionType_Interfere ||
      command.desiredFunctionType == e_FunctionType_Deflect) {
    DO_VALIDATION;
    query.byIncomingBallDirection = true;
    query.incomingBallDirection = (currentMentalImage->GetBallPrediction(180) - currentMentalImage->GetBallPrediction(120)).GetRotated2D(-spatialState.angle).GetNormalized(Vector3(0));
  }

  if (CastPlayer()->AllowLastDitch()) {
    DO_VALIDATION;
    query.allowLastDitchAnims = true;
  } else {
    query.allowLastDitchAnims = false;
  }

  if (command.desiredFunctionType == e_FunctionType_Trip) {
    DO_VALIDATION;
    query.byTripType = true;
    query.tripType = command.tripType;
  }

  query.properties.set("incoming_special_state", currentAnim.anim->GetVariableCache().outgoing_special_state());
  if (match->GetBallRetainer() == player) query.properties.set("incoming_retain_state", currentAnim.anim->GetVariable("outgoing_retain_state"));
  if (command.useSpecialVar1) query.properties.set_specialvar1(command.specialVar1);
  if (command.useSpecialVar2) query.properties.set_specialvar2(command.specialVar2);

  if (!currentAnim.anim->GetVariableCache().outgoing_special_state().empty()) query.incomingVelocity = e_Velocity_Idle; // standing up anims always start out idle

  DataSet dataSet;
  anims->CrudeSelection(dataSet, query);
  if (dataSet.size() == 0) {
    DO_VALIDATION;
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      DO_VALIDATION;
      dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
    } else
      return false;
  }

  //printf("dataset size after crude selection: %i\n", dataSet.size());

  if (command.useDesiredMovement) {
    DO_VALIDATION;

    Vector3 relDesiredDirection = command.desiredDirection.GetRotated2D(-spatialState.angle);
    float desiredAnimationVelocityFloat = command.desiredVelocityFloat;

    // // special case: 90 degrees cornering is tough with weak foot [hax version]
    // radian angle = command.desiredDirection.GetAngle2D(spatialState.directionVec);
    // if (command.desiredFunctionType == e_FunctionType_BallControl && adaptedDesiredVelocityFloat > dribbleVelocity && spatialState.floatVelocity > dribbleVelocity &&
    //     std::fabs(angle) > 0.3 * pi && std::fabs(angle) < 0.8 * pi && angle > 0) adaptedDesiredVelocityFloat = idleVelocity;

    SetMovementSimilarityPredicate(relDesiredDirection, FloatToEnumVelocity(desiredAnimationVelocityFloat));
    SetBodyDirectionSimilarityPredicate(command.desiredLookAt);

    if (command.desiredFunctionType == e_FunctionType_Movement) {
      DO_VALIDATION;
      // this makes body dirs lots better, at the cost of less correct movement anims. todo: maybe it's an idea to actually use this, and then allow more deviation in the physics code to fix the incorrect movement.
      //if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, false, 0.5f * pi);

      // now strict-select from the remainder
      _KeepBestDirectionAnims(dataSet, command, true);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, true);
    }

    else if (command.desiredFunctionType == e_FunctionType_BallControl) {
      DO_VALIDATION;
      bool strict = true;
      if (CastPlayer()->AllowLastDitch()) {
        DO_VALIDATION;
        strict = false;
      }
      float allowedBaseAngle = 0.0f * pi;
      int allowedVelocitySteps = 0; // last ditch anims are always allowed > 0 velocity steps, as long as strict is false
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle); // needed for idle outgoing velo, probably
      _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps);
    }

    else if (command.desiredFunctionType == e_FunctionType_Trap) {
      DO_VALIDATION;

      /*
      bool haste = false;
      // doesn't work well for long anims: they may be disregarded early on as
      panicky, yet then missed when we are starting to panic because they have a
      long 'fadein' if (currentAnim.functionType != e_FunctionType_Trap) {
      DO_VALIDATION; float hasteFactor = GetHasteFactor(false); if (hasteFactor
      > 0.5f) hasteFactor = true; std::string hasteString = hasteFactor ? "YES!
      PANIC!" : "nah relax bro";
      }*/

      bool strict = true;
      if (CastPlayer()->AllowLastDitch(false) || _HighOrBouncyBall()) strict = false;
      float allowedBaseAngle = 0.3f * pi;
      int allowedVelocitySteps = 2;
      int bestBallControlQuadrantID = -1;
      _KeepBestDirectionAnims(dataSet, command, strict, allowedBaseAngle, allowedVelocitySteps, bestBallControlQuadrantID);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedBaseAngle);

      // when too unlike command's desired movement, just don't go for it (and hope for another ballcontrol/trap anim to save us later on)
      if (!_HighOrBouncyBall() && query.allowLastDitchAnims == false) {
        DO_VALIDATION;
        assert(!dataSet.empty());
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        Animation *bestWeGot = anims->GetAnim(*dataSet.begin());
        Vector3 bestWeGotMovement = bestWeGot->GetOutgoingMovement().GetRotated2D(spatialState.angle);
        float currentDesiredDot = command.desiredDirection.GetDotProduct(spatialState.directionVec);

        bool allowAnim = true;

        radian angleDiff = std::fabs(bestWeGot->GetOutgoingDirection().GetRotated2D(spatialState.angle).GetAngle2D(command.desiredDirection));
        if (angleDiff > 0.375f * pi) {
          DO_VALIDATION;  // so we accept at least either 000 or 135 deg anims,
                          // which are two common anim types that are often
                          // available
          allowAnim = false;
        }

        Vector3 desiredBestDiff = bestWeGotMovement - desiredMovement;
        if ((desiredBestDiff.GetLength() > walkVelocity + 0.5f &&
             currentDesiredDot > 0.0f) ||
            (desiredBestDiff.GetLength() > sprintVelocity + 0.5f &&
             currentDesiredDot <= 0.0f)) {
          DO_VALIDATION;  // + margin
          allowAnim = false;
        }

        if (!allowAnim) {
          DO_VALIDATION;
          return false;
        }
      }

    }

    else if (command.desiredFunctionType == e_FunctionType_Interfere) {
      DO_VALIDATION;

      bool strict = false;
      float allowedAngle = 0.3f * pi;
      int allowedVelocitySteps = 1;
      if (command.strictMovement == e_StrictMovement_True) strict = true;

      _KeepBestDirectionAnims(dataSet, command, strict, allowedAngle, allowedVelocitySteps);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command, strict, allowedAngle);
    }
  }

  GetContext().tracker_disabled++;
  std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::ComparePriorityVariable, this, _1, _2));

  int desiredIdleLevel = 0;
  if (!match->IsInPlay()) desiredIdleLevel = 2;
  if (match->IsInSetPiece()) desiredIdleLevel = 1;
  else if ((match->GetBall()->Predict(200) - spatialState.position).GetLength() > 16.0f) desiredIdleLevel = 1;
  SetIdlePredicate(desiredIdleLevel);
  std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareIdleVariable, this, _1, _2));

  // H3e4e2 measurement: strict counterfactual for the foot tie-break. The clone
  // is taken before the foot stable_sort and then receives exactly the same
  // remaining sorts, so the only difference between the two orders is foot.
  const bool foot_counterfactual =
      command.desiredFunctionType == e_FunctionType_Movement;
  DataSet withoutFootSort;
  if (foot_counterfactual) withoutFootSort = dataSet;
  SetFootSimilarityPredicate(spatialState.foot);
  std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareFootSimilarity, this, spatialState.foot, _1, _2));

  if (command.desiredFunctionType != e_FunctionType_BallControl) {
    DO_VALIDATION;
    SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
    std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareIncomingBodyDirectionSimilarity, this, _1, _2));
  }

  // moved down
  SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
  std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareIncomingVelocitySimilarity, this, _1, _2));

  // OLD METHOD
  if (command.useDesiredTripDirection) {
    DO_VALIDATION;
    Vector3 relDesiredTripDirection = command.desiredTripDirection.GetRotated2D(-spatialState.angle);
    SetTripDirectionSimilarityPredicate(relDesiredTripDirection);
    std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareTripDirectionSimilarity, this, _1, _2));
  }

  // OLD METHOD
  if (command.desiredFunctionType != e_FunctionType_Movement) {
    DO_VALIDATION;
    std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
  }

  if (command.desiredFunctionType == e_FunctionType_Deflect) {
    DO_VALIDATION;
    std::stable_sort(dataSet.begin(), dataSet.end(), boost::bind(&Humanoid::CompareCatchOrDeflect, this, _1, _2));
  }

  if (foot_counterfactual) {
    // Same remaining chain, minus the foot sort. Conditions mirror production
    // so this stays a counterfactual and not a second scheduler.
    if (command.desiredFunctionType != e_FunctionType_BallControl) {
      SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), boost::bind(&Humanoid::CompareIncomingBodyDirectionSimilarity, this, _1, _2));
    }
    SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
    std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), boost::bind(&Humanoid::CompareIncomingVelocitySimilarity, this, _1, _2));
    if (command.useDesiredTripDirection) {
      SetTripDirectionSimilarityPredicate(command.desiredTripDirection.GetRotated2D(-spatialState.angle));
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), boost::bind(&Humanoid::CompareTripDirectionSimilarity, this, _1, _2));
    }
    if (command.desiredFunctionType != e_FunctionType_Movement) {
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), boost::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
    }
    if (command.desiredFunctionType == e_FunctionType_Deflect) {
      std::stable_sort(withoutFootSort.begin(), withoutFootSort.end(), boost::bind(&Humanoid::CompareCatchOrDeflect, this, _1, _2));
    }
    if (!dataSet.empty() && !withoutFootSort.empty()) {
      RecordFootCounterfactual(*dataSet.begin(), *withoutFootSort.begin(),
                               anims.get());
    }
  }
  GetContext().tracker_disabled--;

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
    DO_VALIDATION;
    dataSet.push_back(GetIdleMovementAnimID()); // do with idle anim (should not happen too often, only after weird bumps when there's for example a need for a sprint anim at an impossible body angle, after a trip of whatever)
  }

  Vector3 desiredBodyDirectionRel = Vector3(0, -1, 0);
  if (command.useDesiredLookAt) desiredBodyDirectionRel = (command.desiredLookAt - (spatialState.position + spatialState.movement * 0.1f)).GetNormalized(Vector3(0, -1, 0)).GetRotated2D(-spatialState.angle);

  if (command.desiredFunctionType == e_FunctionType_Movement ||
      command.desiredFunctionType == e_FunctionType_Trip ||
      command.desiredFunctionType == e_FunctionType_Special) {
    DO_VALIDATION;

    selectedAnimID = *dataSet.begin();
    Animation *nextAnim = anims->GetAnim(selectedAnimID);
    Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
    assert(desiredMovement.coords[2] == 0.0f);
    Vector3 physicsVector = CalculatePhysicsVector(nextAnim, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, rotationSmuggle_tmp);
  } else if (command.desiredFunctionType == e_FunctionType_BallControl) {
    DO_VALIDATION;
    if (NeedTouch(*dataSet.begin(), command)) {
      DO_VALIDATION;
      selectedAnimID = GetBestCheatableAnimID(dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim, preferPassAndShot);
    }
  } else if (command.desiredFunctionType == e_FunctionType_Trap ||
             command.desiredFunctionType == e_FunctionType_Interfere ||
             command.desiredFunctionType == e_FunctionType_Deflect) {
    DO_VALIDATION;
    selectedAnimID = GetBestCheatableAnimID(dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim, preferPassAndShot);
  } else if (command.desiredFunctionType == e_FunctionType_ShortPass ||
             command.desiredFunctionType == e_FunctionType_LongPass ||
             command.desiredFunctionType == e_FunctionType_HighPass ||
             command.desiredFunctionType == e_FunctionType_Shot) {
    DO_VALIDATION;

    selectedAnimID = GetBestCheatableAnimID(dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim);
  } else if (command.desiredFunctionType == e_FunctionType_Sliding) {
    DO_VALIDATION;
    selectedAnimID = GetBestCheatableAnimID(dataSet, command.useDesiredMovement, command.desiredDirection, command.desiredVelocityFloat, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, touchFrame_tmp, radiusOffset_tmp, touchPos_tmp, fullActionSmuggle_tmp, actionSmuggle_tmp, rotationSmuggle_tmp, localInterruptAnim);
    if (selectedAnimID == -1) {
      DO_VALIDATION;
      if (dataSet.size() > 0) {
        DO_VALIDATION;
        selectedAnimID = *dataSet.begin();
        Animation *nextAnim = anims->GetAnim(selectedAnimID);
        Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
        assert(desiredMovement.coords[2] == 0.0f);
        Vector3 physicsVector = CalculatePhysicsVector(nextAnim, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, rotationSmuggle_tmp);
      }
    }
  }

  // check if we really want to requeue; if the anim we dug up is actually better than the current

  if (localInterruptAnim == e_InterruptAnim_ReQueue && selectedAnimID != -1 &&
      currentAnim.positions.size() > 1 && positions_tmp.size() > 1) {
    DO_VALIDATION;

    // don't requeue to same quadrant
    if (action.type == command.desiredFunctionType &&

        ((FloatToEnumVelocity(currentAnim.anim->GetOutgoingVelocity()) !=
              e_Velocity_Idle &&
          currentAnim.anim->GetVariableCache().quadrant_id() ==
              anims->GetAnim(selectedAnimID)
                  ->GetVariableCache()
                  .quadrant_id()) ||
         ((FloatToEnumVelocity(currentAnim.anim->GetOutgoingVelocity()) ==
               e_Velocity_Idle &&
           FloatToEnumVelocity(
               anims->GetAnim(selectedAnimID)->GetOutgoingVelocity()) ==
               e_Velocity_Idle) &&
          std::fabs((ForceIntoPreferredDirectionAngle(
                    currentAnim.anim->GetOutgoingAngle()) -
                ForceIntoPreferredDirectionAngle(
                    anims->GetAnim(selectedAnimID)->GetOutgoingAngle()))) <
              0.06f * pi))) {
      DO_VALIDATION;

      selectedAnimID = -1;
    }
  }

  // make it so

  if (selectedAnimID != -1) {
    DO_VALIDATION;
    previousAnim_frameNum = currentAnim.frameNum;
    previousAnim_functionType = currentAnim.functionType;

    currentAnim.anim = anims->GetAnim(selectedAnimID);
    currentAnim.id = selectedAnimID;
    currentAnim.functionType = command.desiredFunctionType;//StringToFunctionType(currentAnim.anim->GetVariable("type"));
    currentAnim.frameNum = 0;
    currentAnim.touchFrame = touchFrame_tmp;
    currentAnim.originatingInterrupt = localInterruptAnim;
    currentAnim.touchPos = touchPos_tmp;
    currentAnim.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, spatialState.relBodyAngleNonquantized - currentAnim.anim->GetIncomingBodyAngle()) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f), bodyRotationSmoothingMaxAngle * (currentAnim.functionType == e_FunctionType_Movement ? 1.0f : 0.5f));
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
                                    action.elapsedTime_ms, localInterruptAnim,
                                    command, currentAnim.anim);
    CastPlayer()->SetSimulationMovementCommand(command);
    currentAnim.movementSmuggle = CalculateMovementSmuggle(command.desiredDirection, command.desiredVelocityFloat);
    currentAnim.movementSmuggleOffset = Vector3(0);
    CastPlayer()->BeginSimulationAction();
    return true;
  }

  return false;
}

bool Humanoid::NeedTouch(int animID, const PlayerCommand &command) {
  DO_VALIDATION;

  // when idle (and desiredvelo is idle as well), don't want to touch the ball every frame

  Animation *anim = anims->GetAnim(animID);

  if (FloatToEnumVelocity(anim->GetOutgoingVelocity() != e_Velocity_Idle)) return true;
  if (command.desiredVelocityFloat > idleDribbleSwitch) return true;
  if (std::fabs(match->GetBall()->GetMovement().GetLength()) > 2.0f) return true;

  Vector3 animMovement = anim->GetOutgoingMovement().GetRotated2D(spatialState.angle) * 0.3f + spatialState.movement * 0.7f;

  float animVelo = animMovement.GetLength();
  animMovement.Normalize(spatialState.directionVec);
  animMovement *= spatialState.movement.GetLength() * 0.8f + animVelo * 0.2f;
  auto currentMentalImage = match->GetMentalImage(mentalImageTime);
  Vector3 ballMovement = (currentMentalImage->GetBallPrediction(250).Get2D() - currentMentalImage->GetBallPrediction(240).Get2D()) * 100;

  if (std::fabs(anim->GetOutgoingAngle()) > 0.125f * pi) return true;

  float distanceDeviation = (animMovement - ballMovement).GetLength();
  if (distanceDeviation >= 2.0) return true;

  float velocityDeviation = animMovement.GetLength() - ballMovement.GetLength(); // negative == ball is faster
  if (velocityDeviation < -1.4 || velocityDeviation >= 0.7) return true;

  if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) != e_Velocity_Idle) {
    DO_VALIDATION;
    float angleDeviation = animMovement.GetNormalized(spatialState.directionVec).GetDotProduct(ballMovement.GetNormalized(spatialState.directionVec));
    if (angleDeviation < 0.975) return true;
  }

  return false;
}

float Humanoid::GetBodyBallDistanceAdvantage(const Animation *anim, e_FunctionType functionType, const Vector3 &animTouchMovement, const Vector3 &touchMovement, const Vector3 &incomingMovement, const Vector3 &outgoingMovement, radian outgoingAngle, /*const Vector3 &animBallToBall2D, */const Vector3 &bodyPos, const Vector3 &FFO, const Vector3 &animBallPos2D, const Vector3 &actualBallPos2D, const Vector3 &ballMovement2D, float radiusFactor, float radiusCheatDistance, float decayPow, bool debug) const {

  assert(touchMovement.coords[2] == 0.0f);
  assert(bodyPos.coords[2] == 0.0f);

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
  float velocityChange_mps = velocityChange / (anim->GetFrameCount() * 0.01f);

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
    DO_VALIDATION;
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
    DO_VALIDATION;
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
    < 0.0f) { DO_VALIDATION;
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
    DO_VALIDATION;
    result = 0.0f;
  }

  return result;
}

signed int Humanoid::GetBestCheatableAnimID(const DataSet &sortedDataSet, bool useDesiredMovement, const Vector3 &desiredDirection, float desiredVelocityFloat, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, int &animTouchFrame_ret, float &radiusOffset_ret, Vector3 &touchPos_ret, Vector3 &fullActionSmuggle_ret, Vector3 &actionSmuggle_ret, radian &rotationSmuggle_ret, e_InterruptAnim localInterruptAnim, bool preferPassAndShot) const {

  // never allow touchanims when someone else is holding the ball in his/her hands
  if (match->GetBallRetainer() != 0 && match->GetBallRetainer() != player) return -1;

  Vector3 incomingMovement = spatialState.movement.GetRotated2D(-spatialState.angle);

  signed int bestAnimID = -1;
  Vector3 bestActionSmuggleVec2D;
  Vector3 bestTouchMovementAbs;

  DataSet::const_iterator iter = sortedDataSet.begin();

  Vector3 desiredMovement = desiredDirection * desiredVelocityFloat;

  float playerHeight = player->GetPlayerData()->GetHeight();

  e_FunctionType functionType =
      StringToFunctionType(anims->GetAnim(*iter)->GetAnimType());

  radian rotationSmuggle_ret_tmp = 0;
  radian predictedAngle = 0;
  Vector3 adaptedOutgoingMovement;

  Vector3 dud;

  bool found = false;
  while (iter != sortedDataSet.end() && found == false) {
    DO_VALIDATION;

    Animation *anim = anims->GetAnim(*iter);
    bool isBase = anim->GetVariableCache().baseanim();

    const std::vector<Vector3> &origPositionCache = match->GetAnimPositionCache(anim);

    Vector3 physicsVector = CalculatePhysicsVector(anim, useDesiredMovement, desiredMovement, useDesiredBodyDirection, desiredBodyDirectionRel, positions_ret, rotationSmuggle_ret_tmp);

    // anim space!
    predictedAngle = anim->GetOutgoingAngle() + rotationSmuggle_ret_tmp;
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

    int frameCount = anim->GetEffectiveFrameCount();

    boost::shared_ptr<FootballAnimationExtension> footballExtension = boost::static_pointer_cast<FootballAnimationExtension>(anim->GetExtension("football"));

    int totalTouches = footballExtension->GetTouchCount();
    int touchIDs[totalTouches];
    int count = 0;

    int defaultTouchFrame = atoi(anim->GetVariable("touchframe").c_str());
    assert(defaultTouchFrame >= 0 && defaultTouchFrame < frameCount);

    // first the middle one down to the first
    for (int i = totalTouches / 2; i > -1; i--) {
      DO_VALIDATION;
      touchIDs[count] = i;
      count++;
    }
    // then the 1-after-middle one and upwards
    for (int i = totalTouches / 2 + 1; i < totalTouches; i++) {
      DO_VALIDATION;
      touchIDs[count] = i;
      count++;
    }

    while (touchNum < totalTouches && found == false) {
      DO_VALIDATION;

      bool exists = footballExtension->GetTouch(touchIDs[touchNum], animBallPos, animTouchFrame);
      assert(exists);

      // out of bounds?
      if (match->GetBallRetainer() != player) {
        DO_VALIDATION;
        Vector3 absBallPos = match->GetBall()->Predict(animTouchFrame * 10);
        if (std::fabs(absBallPos.coords[0]) > pitchHalfW + lineHalfW + 0.11f ||
            std::fabs(absBallPos.coords[1]) > pitchHalfH + lineHalfW + 0.11f) {
          DO_VALIDATION;
          touchNum++;
          continue;
        }
      }

      Vector3 touchMovement = CalculateMovementAtFrame(positions_ret, animTouchFrame).GetRotated2D(-spatialState.angle);
      Vector3 animTouchMovement = CalculateMovementAtFrame(origPositionCache, animTouchFrame);// already anim space so no: .GetRotated2D(-spatialState.angle);

      Vector3 ballPos, ballMovement;
      auto mentalImage = match->GetMentalImage(mentalImageTime);
      ballPos = mentalImage->GetBallPrediction(animTouchFrame * 10);
      ballMovement = (mentalImage->GetBallPrediction(animTouchFrame * 10 + 10) - mentalImage->GetBallPrediction(animTouchFrame * 10)) * 100.0f;
      ballPos = (ballPos - spatialState.position).GetRotated2D(-spatialState.angle);
      ballMovement = ballMovement.GetRotated2D(-spatialState.angle);

      bodyPos = positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      bodyPos.coords[2] = 0;

      anim->GetKeyFrame(BodyPart::player, animTouchFrame, animBodyRot, animBodyPos);

      real x, y, z;
      animBodyRot.GetAngles(x, y, z);

      float animBallHeight = animBallPos.coords[2];
      if (allowPreTouchRotationSmuggle) {
        DO_VALIDATION;
        animBallPos = (animBallPos - animBodyPos).GetRotated2D(rotationSmuggle_ret_tmp * ((float)animTouchFrame / (float)frameCount)) + positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      } else {
        animBallPos = (animBallPos - animBodyPos) + positions_ret.at(animTouchFrame).GetRotated2D(-spatialState.angle);
      }
      animBallPos.coords[2] = animBallHeight * (playerHeight / defaultPlayerHeight);

      // now pick the ballPos from the previous 9ms that is closest to animBallPos. this is to emulate a continuous 'close enough?'-check instead of a 'single moment' check.
      if (useContinuousBallCheck) {
        DO_VALIDATION;
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
      if (match->GetBallRetainer() == player) ballDistanceZ = 0.0f;

      if (ballPos.coords[2] < 0.5f && isBase) ballDistanceZ = std::max(ballDistanceZ - 0.15f, 0.0f); // low balls should be doable with ground level anims, doesn't look that bad :P

      if (ballDistanceZ < 0.22f) {
        DO_VALIDATION;

        // default touch can be 'cheated' towards best, has biggest 'radius'
        float touchFrameAwkwardness = NormalizedClamp(std::abs(defaultTouchFrame - animTouchFrame), 0.0f, 4.0f);
        touchFrameAwkwardness = std::pow(touchFrameAwkwardness, 2.0f) * 0.5f;

        /* todo: check if this is a possibility in some form
              // add this to desired cheatvec to get more of a flowing feeling to it
              Vector3 flowVector = spatialState.movement - match->GetBall()->GetMovement().Get2D();// - anims->GetAnim(*iter)->GetOutgoingMovement().GetRotated2D(spatialState.angle) * 2.0;
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
          DO_VALIDATION;
          radiusFactor *= 1.8f;
          radiusCheatOffset += 0.4f;
        }

        if (functionType == e_FunctionType_Sliding) {
          DO_VALIDATION;
          radiusFactor *= 0.2f;
          radiusCheatOffset = 0.0f;
        }  // prefer sliding without ball touch
        if (functionType == e_FunctionType_Interfere) {
          DO_VALIDATION;
          radiusFactor *= 1.4f;
          radiusCheatOffset += 0.2f;
        }

        if (functionType == e_FunctionType_ShortPass) {
          DO_VALIDATION;
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_LongPass) {
          DO_VALIDATION;
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_HighPass) {
          DO_VALIDATION;
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }
        if (functionType == e_FunctionType_Shot) {
          DO_VALIDATION;
          radiusFactor *= 1.3f;
          radiusCheatOffset += 0.15f;
        }

        if ((functionType == e_FunctionType_Trap ||
             functionType == e_FunctionType_BallControl) &&
            preferPassAndShot == true) {
          DO_VALIDATION;
          radiusFactor *= 0.3f;
        }

        radiusCheatOffset *= (1.0f - touchFrameAwkwardness);

        float touchVelo = touchMovement.GetLength();

        Vector3 FFO = GetFrontOfFootOffsetRel(touchVelo, z, ballPos.coords[2]);
        if (FloatToEnumVelocity(touchVelo) == e_Velocity_Idle) {
          DO_VALIDATION;
          //FFO.Rotate2D(z); // always towards y = -1, right? (hmmm not really)
        } else {
          FFO.Rotate2D(FixAngle(touchMovement.GetNormalized(Vector3(0, -1, 0)).GetAngle2D()));
        }
        // just touched ball
        float lastTouchBias = curve(player->GetLastTouchBias(600, match->GetActualTime_ms() + animTouchFrame * 10), 1.0f);
        if (lastTouchBias > 0.0f) {
          DO_VALIDATION;
          float factor = 1.0f - lastTouchBias * 0.97f * (1.0f - player->GetStat(technical_ballcontrol) * 0.1f);
          radiusFactor *= factor;
          radiusCheatOffset *= factor;
        }

        bool debug = false;

        float touchFramedRadiusFactor = radiusFactor * touchFrameFactor;
        float bodyBallDistanceAdvantage = GetBodyBallDistanceAdvantage(anim, functionType, animTouchMovement, touchMovement, incomingMovement, adaptedOutgoingMovement, predictedAngle, bodyPos, FFO, animBallPos.Get2D(), ballPos.Get2D(), ballMovement.Get2D(), touchFramedRadiusFactor, radiusCheatOffset, 1.0f, debug);

        if (bodyBallDistanceAdvantage >= 1.0f ||
            match->GetBallRetainer() == player) {
          DO_VALIDATION;
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
    DO_VALIDATION;
    auto currentMentalImage = match->GetMentalImage(mentalImageTime);
    touchPos_ret = currentMentalImage->GetBallPrediction(animTouchFrame_ret * 10);

    fullActionSmuggle_ret = bestActionSmuggleVec2D.GetRotated2D(spatialState.angle);
    actionSmuggle_ret = fullActionSmuggle_ret;

    if (forceFullActionSmuggleDiscard) {
      DO_VALIDATION;

      actionSmuggle_ret = 0;

    } else if (enableActionSmuggleDiscard) {
      DO_VALIDATION;

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

      if (match->GetBallRetainer() == player) smuggleDistance = 0.0f;
      actionSmuggle_ret = actionSmuggle_ret.GetNormalized(0) * smuggleDistance;

      // lose forward-facing part of smuggle

      if (discardForwardSmuggle || discardSidewaysSmuggle) {
        DO_VALIDATION;

        radian toStraightAngle = spatialState.angle + predictedAngle;
        actionSmuggle_ret.Rotate2D(-toStraightAngle);

        if (discardForwardSmuggle) {
          DO_VALIDATION;
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
          DO_VALIDATION;
          actionSmuggle_ret.coords[0] *= 0.7f;
        }

        actionSmuggle_ret.Rotate2D(toStraightAngle);
      }

      // less chaos in micro battles
      actionSmuggle_ret *= 0.7f + 0.3f * NormalizedClamp(CastPlayer()->GetClosestOpponentDistance(), 0.6f, 1.2f);
    }

    assert(actionSmuggle_ret.coords[2] == 0.0f);
  }

  rotationSmuggle_ret = rotationSmuggle_ret_tmp;

  return bestAnimID;
}

Vector3 Humanoid::CalculateMovementSmuggle(const Vector3 &desiredDirection,
                                           float desiredVelocityFloat) {
  DO_VALIDATION;

  if (!enableMovementSmuggle) return Vector3(0);

  if (team->GetDesignatedTeamPossessionPlayer() != player || match->GetDesignatedPossessionPlayer() != player ||
      currentAnim.touchFrame != -1 || (currentAnim.functionType == e_FunctionType_Trip && currentAnim.anim->GetVariable("triptype").compare("1") != 0) || currentAnim.anim->GetVariableCache().incoming_special_state().compare("") != 0 || currentAnim.anim->GetVariableCache().outgoing_special_state().compare("") != 0 ||
      !match->IsInPlay() || match->IsInSetPiece() || match->GetBallRetainer() != 0) return Vector3(0);

  Vector3 toDesired;

  // various stuff needed by all

  unsigned int timeToBall_ms = CastPlayer()->GetTimeNeededToGetToBall_ms();
  if (CastPlayer()->GetDesiredTimeToBall_ms() > (signed int)timeToBall_ms) {
    DO_VALIDATION;
    timeToBall_ms = CastPlayer()->GetDesiredTimeToBall_ms();
  }
  unsigned int animTime_ms = currentAnim.anim->GetFrameCount() * 10;
  unsigned int futureTime_ms = std::max(animTime_ms + defaultTouchOffset_ms, timeToBall_ms);

  Vector3 predictedOutgoingMovement = CalculateOutgoingMovement(currentAnim.positions);
  Vector3 predictedPos;
  radian predictedAngle;
  CalculatePredictedSituation(predictedPos, predictedAngle);
  Vector3 ballPos = match->GetMentalImage(mentalImageTime)->GetBallPrediction(futureTime_ms);
  float ballHeight = ballPos.coords[2];
  Vector3 ffo = GetFrontOfFootOffsetRel(predictedOutgoingMovement.GetLength(), currentAnim.anim->GetOutgoingBodyAngle(), ballHeight).GetRotated2D(predictedAngle);
  Vector3 desiredBallPos = predictedPos + ffo;

  if (!CastPlayer()->HasPossession()) {
    DO_VALIDATION;

    // macro effect: consider a line going in the ball movement direction. consider the spot we want the ball at (in front of us) after this movement anim.
    // now calculate the shortest line between that line and that point. now move over that line from the point towards the line somewhat

    Line ballMovementLine;
    ballMovementLine.SetVertex(0, match->GetMentalImage(mentalImageTime)->GetBallPrediction(0).Get2D());
    ballMovementLine.SetVertex(1, match->GetMentalImage(mentalImageTime)->GetBallPrediction(futureTime_ms).Get2D());
    if (ballMovementLine.GetLength() < 0.5f) return Vector3(0); // ball is slow or very close

    float u = ballMovementLine.GetClosestToPoint(desiredBallPos);
    Vector3 closestBallPos = ballMovementLine.GetVertex(0) + (ballMovementLine.GetVertex(1) - ballMovementLine.GetVertex(0)) * u;

    toDesired = closestBallPos - desiredBallPos;

  } else {  // if HasPossession

    toDesired = ballPos.Get2D() - desiredBallPos;
  }

  unsigned int maxEffectTimeTreshold_ms = 250 + defaultTouchOffset_ms; // if the ball is this much longer 'farther away' than feasible, rather postpone effect until next anim (else we may overrun)
  if (FloatToEnumVelocity(currentAnim.anim->GetOutgoingVelocity()) == e_Velocity_Idle) maxEffectTimeTreshold_ms = 2000; // no danger of overrunning
  float maxEffectVelocity = dribbleWalkSwitch;
  float maxSmuggleMPS = 1.6f;

  if (futureTime_ms - (animTime_ms + defaultTouchOffset_ms) > maxEffectTimeTreshold_ms) return Vector3(0);

  Vector3 toDesiredMovement = toDesired / (animTime_ms * 0.001f);
  Vector3 resultingMovement = predictedOutgoingMovement + toDesiredMovement;
  float predictedVelocity = predictedOutgoingMovement.GetLength();
  float resultingVelocity = resultingMovement.GetLength();
  if (resultingVelocity > predictedVelocity && resultingVelocity > maxEffectVelocity) return Vector3(0);

  toDesired.NormalizeMax(maxSmuggleMPS * (currentAnim.anim->GetEffectiveFrameCount() * 0.01f));

  //SetGreenDebugPilon(predictedPos);

  float removeDistance = 0.06f; // remove part of the smuggle (allow staying this far away from ideal spot)
  toDesired = toDesired.GetNormalized(0) * std::max(0.0f, toDesired.GetLength() - removeDistance);
  return toDesired;
}

Vector3 Humanoid::GetBestPossibleTouch(const Vector3 &desiredTouch,
                                       e_FunctionType functionType) {
  DO_VALIDATION;
  constexpr float maxPowerShortPass = 30.0f;
  constexpr float maxPowerHighPass  = 42.0f;
  float maxPowerBase = maxPowerShortPass;
  if (functionType == e_FunctionType_HighPass) maxPowerBase = maxPowerHighPass;

  Vector3 resultTouch = desiredTouch;

  // fetch vars

  float maxPowerFactor = atof(currentAnim.anim->GetVariable("touch_maxpowerfactor").c_str());
  if (maxPowerFactor == 0.0f) maxPowerFactor = 1.0f;
  maxPowerFactor = maxPowerFactor * 0.7f + 0.3f;

  // clamp to maximum possible power (from anim vars)

  float maxPower = maxPowerBase * maxPowerFactor * (1.0f - clamp(decayingPositionOffset.GetLength() * 2.5f, 0.0f, 0.25f));
  maxPower += match->GetBall()->GetMovement().GetLength() * 0.5f; // can use some of current ballmomentum
  if (resultTouch.GetLength() > maxPower) {
    DO_VALIDATION;
    float missingPower = resultTouch.GetLength() - maxPower;
    resultTouch = resultTouch.GetNormalized(0) * maxPower;
    resultTouch.coords[2] += clamp(missingPower, 0.0f, 10.0f) * 0.25f;
  }

  // difficulty

  float difficultyFactor = atof(currentAnim.anim->GetVariable("touch_difficultyfactor").c_str());

  // apply stats
  if (functionType == e_FunctionType_ShortPass ||
      functionType == e_FunctionType_LongPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(technical_shortpass) * 0.5f);
  if (functionType == e_FunctionType_HighPass) difficultyFactor *= (1.0f - CastPlayer()->GetStat(technical_highpass)  * 0.5f);

  float distanceFactor = 0.0f;
  float heightFactor = 0.0f;
  float ballMovementFactor = 0.0f;
  GetDifficultyFactors(match, CastPlayer(), spatialState, decayingPositionOffset, distanceFactor, heightFactor, ballMovementFactor);

  // difficult balls may go into a more random orientation, or, if the anim has a default outgoing direction, it may converge towards that (since it is the easiest direction for that anim)
  radian randomRotation = 0.0f;
  randomRotation = distanceFactor * 0.15f + heightFactor * 0.15f + ballMovementFactor * 0.3f + difficultyFactor * 0.5f;
  Vector3 animBallDirection = GetVectorFromString(currentAnim.anim->GetVariable("balldirection")).GetRotated2D(startAngle + currentAnim.rotationSmuggleOffset);
  if (animBallDirection.GetLength() > 0.01f) {
    DO_VALIDATION;
    float bias = clamp(randomRotation * 1.5f, 0.0f, 1.0f);
    Vector3 nativeTouch = animBallDirection.GetNormalized(resultTouch).Get2D() * resultTouch.GetLength() + resultTouch * Vector3(0, 0, 1);
    resultTouch = resultTouch * (1.0f - bias) + nativeTouch * bias;
  } else {
    radian rotation =
        boostrandom(-0.5f * pi, 0.5f * pi) * std::min((real) randomRotation, 0.5f);
    resultTouch.Rotate2D(rotation);
  }

  // ball far away == less power
  resultTouch *= 1.0f - distanceFactor * 0.3f;
  resultTouch.coords[2] += distanceFactor * 1.5f; // try to correct (add power) by playing higher ball (== less ground friction)

  resultTouch.coords[2] += match->GetBall()->GetMovement().coords[2] * heightFactor * 0.5f +
                           heightFactor * 1.0f;

  resultTouch = resultTouch * (1.0f - ballMovementFactor) +
                match->GetBall()->GetMovement() * ballMovementFactor;

  resultTouch.coords[2] += difficultyFactor * 5.0f * boostrandom(0.2f, 1.0f);

  return resultTouch;
}

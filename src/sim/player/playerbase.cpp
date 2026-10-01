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

#include "support/diagnostics/log.hpp"
#include "sim/player/player.hpp"
#include "sim/player/player_action_executor.hpp"
#include "sim/player/player_locomotion.hpp"
#include "sim/player/locomotion_intent_scheduler.hpp"
#include "sim/player/legacy_locomotion_command.hpp"
#include "sim/player/player_intent_builder.hpp"

#include <cstring>

#include "sim/match.hpp"


int &SimulationOnlyGateMismatchForResetContext(int context) {
  static int records[kResetSituationCallContextCount] = {};
  return records[context];
}

const char *ResetSituationCallContextName(int context) {
  switch (context) {
    case kResetSituationInitialBeforeFirstPlayerTick: return "initial";
    case kResetSituationRuntime: return "runtime";
    case kResetSituationPlayerDeactivateFirst: return "player_deactivate_first";
    case kResetSituationBaseDeactivateSecond: return "base_deactivate_second";
    default: return "unknown";
  }
}
int &MovementOracleConsumesForSource(int source) {
  static int records[5] = {};
  return records[source];
}
int &DecisionLocomotionIntentPresentTicks() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingTicks() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingForSource(int source) {
  static int records[5] = {};
  return records[source];
}
long &DecisionLocomotionIntentAgeSum_ms() { static long value = 0; return value; }
int &DecisionLocomotionIntentAgeCount() { static int value = 0; return value; }
int &DecisionLocomotionIntentAgeMax_ms() { static int value = -1; return value; }
int &ActionCoupledLegacyMismatch() { static int value = 0; return value; }
int &DecisionLocomotionIntentMissingForSourceLegacyGateFalse(int source) {
  static int records[5] = {};
  return records[source];
}
int &LocomotionActionExitCount() { static int value = 0; return value; }
int &ContinuityRepairAttempts() { static int value = 0; return value; }
int &ContinuityRepairPublications() { static int value = 0; return value; }
int &ContinuityRepairCandidatesMissing() { static int value = 0; return value; }
int &DecisionPublicationCauseCount(int cause) { static int records[3] = {}; return records[cause]; }
void PlayerBase::NoteDecisionPublicationCause(int cause) { pendingPublicationCause = cause; }
int &DecisionPublicationViaSimulationCadence() { static int value = 0; return value; }
int &DecisionPublicationViaLegacyOpportunityOnly() { static int value = 0; return value; }
int &DecisionPublicationWhileIneligible() { static int value = 0; return value; }
int &ReentryFreshViaSimulationCadence() { static int value = 0; return value; }
int &ReentryFreshViaLegacyOpportunityOnly() { static int value = 0; return value; }
int &ReentryFreshWhileIneligible() { static int value = 0; return value; }

int &LocomotionNegativeDecisionAgeSamples() { static int value = 0; return value; }
int &LocomotionReentryMeasurementEpoch() { static int value = 0; return value; }
void ResetLocomotionReentryAudits() {
  for (int category = 0; category < 4; ++category) {
    LocomotionReentryAuditFor(category) = LocomotionReentryAudit();
  }
  LocomotionActionExitCount() = 0;
  LocomotionNegativeDecisionAgeSamples() = 0;
  DecisionPublicationViaSimulationCadence() = 0;
  DecisionPublicationViaLegacyOpportunityOnly() = 0;
  DecisionPublicationWhileIneligible() = 0;
  ReentryFreshViaSimulationCadence() = 0;
  ReentryFreshViaLegacyOpportunityOnly() = 0;
  ReentryFreshWhileIneligible() = 0;
  // Per-player provenance is scoped by epoch, so a player resets its own audit
  // state on its next tick without iterating the roster.
  ++LocomotionReentryMeasurementEpoch();
}

void PlayerBase::CheckDecisionLocomotionIntentOracle() const {
  // Producer contract for the executed intent. The executability half lives in
  // HasExecutableDecisionLocomotionIntent(); this half stays fatal so a Direct
  // publication that is not a Movement intent can never silently execute.
  const PlayerCommand &command = decisionLocomotionState.command;
  if (!decisionLocomotionState.initialized || !command.useDesiredMovement ||
      command.desiredFunctionType != e_FunctionType_Movement ||
      decisionLocomotionState.publishedEpoch !=
          decisionLocomotionState.continuityEpoch) {
    Log(e_FatalError, "PlayerBase", "CheckDecisionLocomotionIntentOracle",
        "the executed locomotion intent is not a current-epoch Direct Movement "
        "intent");
  }
}

void PlayerBase::AdvanceLocomotionContinuity(bool eligible) {
  // Gameplay transition. The audit below observes eligibility and must never
  // decide the epoch, or telemetry bookkeeping would drive a controller query.
  if (!decisionLocomotionState.continuityStarted) {
    decisionLocomotionState.continuityStarted = true;
    decisionLocomotionState.wasEligibleLastTick = eligible;
    return;
  }
  if (decisionLocomotionState.wasEligibleLastTick && !eligible) {
    ++decisionLocomotionState.continuityEpoch;
  }
  decisionLocomotionState.wasEligibleLastTick = eligible;
}

void PlayerBase::NoteLocomotionReentryTick(bool eligible, bool scheduler_due,
                                          int now_ms) {
  // Measurement epoch: telemetry is transient and not serialized, so state
  // restore can rewind the match clock behind it. Scope the audit to the epoch
  // instead of letting earlier scenarios pollute it.
  if (reentryAuditEpoch != LocomotionReentryMeasurementEpoch()) {
    reentryAuditEpoch = LocomotionReentryMeasurementEpoch();
    reentryAuditStarted = false;
    locomotionExitRecorded = false;
  }
  const unsigned long long generation = decisionLocomotionAuditGeneration;
  if (!reentryAuditStarted) {
    reentryAuditStarted = true;
    wasPureLocomotionLastTick = eligible;
    resetSinceLastPlayerTick = false;
    return;
  }
  if (!eligible) {
    // An ordinary non-locomotion tick is not a re-entry. Only leaving
    // locomotion is an event, and it arms the generation comparison.
    if (wasPureLocomotionLastTick) {
      decisionGenerationAtLocomotionExit = generation;
      locomotionExitRecorded = true;
      // The epoch itself is advanced by AdvanceLocomotionContinuity(); this is
      // observation only.
      ++LocomotionActionExitCount();
    }
    wasPureLocomotionLastTick = false;
    return;
  }
  // Eligible from here on. An actor that began the audit in an action and later
  // becomes locomotion-eligible has an initial transition, not an observed action
  // re-entry; reserve category 2 for a recorded locomotion exit.
  int category = 0;
  if (resetSinceLastPlayerTick) {
    category = 3;
  } else if (wasPureLocomotionLastTick) {
    category = 1;
  } else if (locomotionExitRecorded) {
    category = 2;
  }
  LocomotionReentryAudit &audit = LocomotionReentryAuditFor(category);
  ++audit.ticks;
  if (scheduler_due) ++audit.scheduler_due; else ++audit.scheduler_not_due;
  // Shadow of the epoch rule. Continuous locomotion must never require a fresh
  // decision just because the cadence is due.
  if (decisionLocomotionState.publishedEpoch !=
      decisionLocomotionState.continuityEpoch) {
    ++audit.would_require_fresh;
    if (lastResetSituation_ms > lastDirectMovementIntentPublication_ms) {
      ++audit.stale_after_reset;
    } else if (category == 2 && locomotionExitRecorded) {
      // a3b2 no longer lets animation opportunities publish the missing axis.
      // A stale action re-entry is repaired by the forced Player Decision tick
      // later in this same Process() call, before locomotion execution.
      ++audit.stale_after_action_exit;
    } else {
      ++audit.stale_not_explained_by_reset;
    }
  }
  if (category >= 2) {
    // Reset is itself the discontinuity, so it anchors the comparison; other
    // re-entries require an observed locomotion exit.
    const bool anchor_valid = category == 3 ? resetGenerationAnchorValid
                                            : locomotionExitRecorded;
    const unsigned long long anchor = category == 3
        ? resetDecisionGeneration
        : decisionGenerationAtLocomotionExit;
    if (anchor_valid && generation != anchor) {
      ++audit.generation_advanced;
      if (lastPublicationViaSimulationCadence) {
        ++ReentryFreshViaSimulationCadence();
      } else {
        ++ReentryFreshViaLegacyOpportunityOnly();
      }
      if (lastPublicationWhileIneligible) ++ReentryFreshWhileIneligible();
    } else if (anchor_valid) {
      ++audit.generation_unchanged;
    }
  }
  if (lastDirectMovementIntentPublication_ms >= 0) {
    const int age_ms = now_ms - lastDirectMovementIntentPublication_ms;
    if (age_ms < 0) {
      // A restorable clock cannot precede a publication in normal forward play.
      ++LocomotionNegativeDecisionAgeSamples();
    } else {
      audit.decision_age_sum_ms += age_ms;
      ++audit.decision_age_count;
      if (age_ms > audit.decision_age_max_ms) audit.decision_age_max_ms = age_ms;
    }
  }
  wasPureLocomotionLastTick = true;
  resetSinceLastPlayerTick = false;
}


int &DecisionLocomotionIntentPresentTicksLegacyGateFalse() {
  static int value = 0;
  return value;
}
int &DecisionLocomotionIntentMissingTicksLegacyGateFalse() {
  static int value = 0;
  return value;
}
LocomotionReentryAudit &LocomotionReentryAuditFor(int category) {
  static LocomotionReentryAudit records[4];
  return records[category];
}
const char *LocomotionReentryCategoryName(int category) {
  switch (category) {
    case 0: return "initial";
    case 1: return "continuous";
    case 2: return "action_reentry";
    case 3: return "reset_reentry";
    default: return "unknown";
  }
}


#include "sim/player/controller/elizacontroller.hpp"
#include "sim/player/controller/strategies/strategy.hpp"

#include "env/main.hpp"
#include "sim/utils.hpp"

#include "foundation/geometry/triangle.hpp"

namespace {

// Bit comparison, not an epsilon: a movement mirror that is only approximately
// right is a synchronization bug, and masking it would let a later procedural
// producer inherit an inconsistent baseline.
bool Vector3BitsEqual(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

bool FloatBitsEqual(float a, float b) {
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

PlayerBase::PlayerBase(Match *match, PlayerData *playerData)
    : match(match),
      playerData(playerData),
      stable_id(GetContext().stablePlayerCount++) {
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  fatigueFactorInv = 1.0;
}

PlayerBase::~PlayerBase() {
  if (isActive) Deactivate();
}

bool PlayerBase::IsKinematicMirrorConsistent() const {
  return Vector3BitsEqual(kinematicState.position, humanoid->GetPosition()) &&
         Vector3BitsEqual(kinematicState.velocity, humanoid->GetMovement()) &&
         Vector3BitsEqual(kinematicState.facing, humanoid->GetDirectionVec()) &&
         Vector3BitsEqual(kinematicState.bodyFacing,
                          humanoid->GetBodyDirectionVec()) &&
         FloatBitsEqual(kinematicState.speed,
                        kinematicState.velocity.GetLength()) &&
         Vector3BitsEqual(groundCollider.center,
                          kinematicState.position.Get2D());
}

void PlayerBase::CheckSimulationKinematicOracle() const {
  const Vector3 &position = humanoid->GetPosition();
  const Vector3 &movement = humanoid->GetMovement();
  const Vector3 &direction = humanoid->GetDirectionVec();
  const Vector3 &bodyDirection = humanoid->GetBodyDirectionVec();

  std::string mismatch;
  if (!Vector3BitsEqual(kinematicState.position, position)) {
    mismatch = "position";
  } else if (!Vector3BitsEqual(kinematicState.velocity, movement)) {
    mismatch = "velocity";
  } else if (!Vector3BitsEqual(kinematicState.facing, direction)) {
    mismatch = "facing";
  } else if (!Vector3BitsEqual(kinematicState.bodyFacing, bodyDirection)) {
    mismatch = "body facing";
  } else if (!FloatBitsEqual(kinematicState.speed,
                             kinematicState.velocity.GetLength())) {
    mismatch = "speed";
  } else if (!Vector3BitsEqual(groundCollider.center, position.Get2D())) {
    mismatch = "collider center";
  }
  if (!mismatch.empty()) {
    Log(e_FatalError, "PlayerBase", "CheckSimulationKinematicOracle",
        "the gameplay kinematic mirror diverged from the Humanoid spatial "
        "state: " + mismatch);
  }
}

void PlayerBase::Mirror() {
  humanoid->Mirror();
  kinematicState.Mirror();
  groundCollider.Mirror();
  CheckSimulationKinematicOracle();
}

void PlayerBase::SynchronizeKinematicState() {
  kinematicState.position = humanoid->GetPosition();
  kinematicState.velocity = humanoid->GetMovement();
  kinematicState.facing = humanoid->GetDirectionVec();
  kinematicState.bodyFacing = humanoid->GetBodyDirectionVec();
  kinematicState.speed = kinematicState.velocity.GetLength();
  groundCollider.SetCenter(kinematicState.position);
  CheckSimulationKinematicOracle();
}

bool PlayerBase::NoteLocomotionIntentCadence(bool legacy_opportunity) {
  decisionMovementSelection = false;
  const float distance_to_ball =
      (match->GetBall()->Predict(0).Get2D() - kinematicState.position).GetLength();
  const int now_ms = static_cast<int>(match->GetActualTime_ms());
  const bool due = locomotionIntentScheduler.Due(now_ms);
  locomotionIntentDueThisTick = false;
  if (due) {
    ++PlayerLocomotionIntentDueTicks();
    // A due tick during a non-locomotion action is not consumed: querying the
    // controller there would hand locomotion an intent that is already hundreds
    // of milliseconds old by the time the action ends. The clock simply stays
    // overdue so the refresh happens on the first eligible tick.
    if (!IsEligibleForProceduralLocomotion()) {
      ++PlayerLocomotionIntentDueIneligibleTicks();
    } else {
      locomotionIntentDueThisTick = true;
    }
  }
  if (legacy_opportunity) {
    ++PlayerLocomotionIntentLegacyOpportunityTicks();
    if (locomotionIntentDueThisTick) ++PlayerLocomotionIntentOverlapTicks();
  }
  return locomotionIntentDueThisTick;
}

void PlayerBase::PublishPlayerDecisionQueue(
    const PlayerCommandQueue &commands, int now_ms) {
  playerDecisionQueue.commands = commands;
  playerDecisionQueue.initialized = true;
  ++playerDecisionQueue.generation;
  playerDecisionScheduler.Commit(now_ms);
}

void PlayerBase::CommitLocomotionIntentRefresh() {
  ++HumanoidIntentRefreshCommits();
  const float distance_to_ball =
      (match->GetBall()->Predict(0).Get2D() - kinematicState.position).GetLength();
  ++PlayerLocomotionIntentConsumedTicks();
  locomotionIntentScheduler.Schedule(
      static_cast<int>(match->GetActualTime_ms()),
      LocomotionIntentScheduler::CadenceForDistance_ms(
          distance_to_ball, match->GetBallRetainer() == this));
}


// 4b': intent refresh is held back purely because execution is not pure. This is
// the conflation under measurement: the scheduler is due, but the execution gate
// blocks the refresh.
bool PlayerBase::LocomotionIntentRefreshHeldIneligible() const {
  return locomotionIntentScheduler.Due(
             static_cast<int>(match->GetActualTime_ms())) &&
         !IsEligibleForProceduralLocomotion();
}

static int t4opp_queries = 0, t4opp_with_candidate = 0;
static int t4opp_cand_not_due = 0, t4opp_cand_due_ineligible = 0, t4opp_cand_due_eligible = 0;
static int t4opp_nocand_not_due = 0, t4opp_nocand_other = 0;

void DumpQueryOpportunities() {
  printf("4opp-SUMMARY queries=%d with_candidate=%d\n", t4opp_queries, t4opp_with_candidate);
  printf("4opp-SUMMARY candidate: not_due=%d due+ineligible=%d due+eligible=%d\n",
         t4opp_cand_not_due, t4opp_cand_due_ineligible, t4opp_cand_due_eligible);
  printf("4opp-SUMMARY no_candidate: not_due=%d other=%d\n", t4opp_nocand_not_due,
         t4opp_nocand_other);
  fflush(stdout);
}
void PlayerBase::NoteControllerQuery(bool had_movement_candidate) {
  const int now_ms = static_cast<int>(match->GetActualTime_ms());
  ++tr_query_gen;
  tr_last_query_ms = now_ms;
  tr_last_query_due = locomotionIntentScheduler.Due(now_ms) ? 1 : 0;
  tr_last_query_eligible = IsEligibleForProceduralLocomotion() ? 1 : 0;
  tr_last_query_action = static_cast<int>(actionState.type);
  tr_last_query_retains = match->GetBallRetainer() == this ? 1 : 0;
  tr_last_query_had_candidate = had_movement_candidate ? 1 : 0;
  ++t4opp_queries;
  if (had_movement_candidate) {
    ++t4opp_with_candidate;
    if (!tr_last_query_due) ++t4opp_cand_not_due;
    else if (!tr_last_query_eligible) ++t4opp_cand_due_ineligible;
    else ++t4opp_cand_due_eligible;
  } else {
    if (!tr_last_query_due) ++t4opp_nocand_not_due;
    else ++t4opp_nocand_other;
  }
}

void PlayerBase::NoteDecisionMovementSelection(bool movement_selected) {
  decisionMovementSelection = movement_selected;
}

void PlayerBase::ObserveSimulationDecisionQueue(
    const PlayerCommandQueue &commands, int now_ms) {
  simulationDecisionQueue.commands = commands;
  simulationDecisionQueue.initialized = true;
  ++simulationDecisionQueue.generation;
  simulationDecisionQueue.updated_ms = now_ms;
}

// c2a: the Player Decision Clock's single publication entry point. It owns the
// decision locomotion state and the publication telemetry, and never touches the
// compatibility movement command slot.
void PlayerBase::PublishDecisionLocomotionIntent(const PlayerCommand &command) {
  if (command.desiredFunctionType != e_FunctionType_Movement ||
      !command.useDesiredMovement) {
    Log(e_FatalError, "PlayerBase", "PublishDecisionLocomotionIntent",
        "the Player Decision Clock published a non-Movement intent");
  }
  // 4f-a1: whether this publication materially rewrites the decision already in
  // force. Only animation-owned (legacy-only) publications are counted, so the
  // number answers how often a requeue actually moved the decision clock.
  const int publication_cause = pendingPublicationCause;
  const bool decision_materially_changed =
      decisionLocomotionState.initialized &&
      MovementCommandDiffersMaterially(decisionLocomotionState.command, command);
  ++PlayerMovementCommandDirectAdoptions();
  lastDirectMovementIntentPublication_ms =
      static_cast<int>(match->GetActualTime_ms());
  // Cause split without changing the player path: a publication on a tick where
  // the simulation cadence was not due can only have come from the animation
  // lifecycle's query opportunity.
  lastPublicationViaSimulationCadence = pendingPublicationCause == 0;
  ++DecisionPublicationCauseCount(pendingPublicationCause);
  pendingPublicationCause = 0;
  lastPublicationWhileIneligible = !IsEligibleForProceduralLocomotion();
  if (lastPublicationViaSimulationCadence) {
    ++DecisionPublicationViaSimulationCadence();
  } else {
    ++DecisionPublicationViaLegacyOpportunityOnly();
  }
  if (publication_cause == 1) {
    ++LegacyOnlyDecisionPublications();
    if (decision_materially_changed) ++LegacyOnlyDecisionMaterialChanges();
    if (decisionMovementSelection) {
      ++LegacyOnlyDecisionMovementSelectionPublications();
      if (decision_materially_changed) {
        ++LegacyOnlyDecisionMovementSelectionMaterialChanges();
      }
    }
  }
  if (lastPublicationWhileIneligible) ++DecisionPublicationWhileIneligible();
  // A publication belongs to the continuity epoch it was produced in, which is
  // what makes it usable for this epoch's locomotion execution.
  decisionLocomotionState.command = command;
  decisionLocomotionState.initialized = true;
  ++decisionLocomotionAuditGeneration;
  decisionLocomotionState.publishedEpoch =
      decisionLocomotionState.continuityEpoch;
}

// Single place that turns a controller queue into a published locomotion intent.
// Returns true only when a Movement candidate with useDesiredMovement was
// published, so callers commit the scheduler refresh only on a real publication.
// Both Process() paths (real players and officials) share it.
bool PlayerBase::PublishMovementIntentFromQueue(const PlayerCommandQueue &queue) {
  for (const PlayerCommand &candidate : queue) {
    if (candidate.desiredFunctionType == e_FunctionType_Movement &&
        candidate.useDesiredMovement) {
      PublishDecisionLocomotionIntent(candidate);
      return true;
    }
  }
  return false;
}


PlayerActionState PlayerBase::CaptureLegacyActionState() const {
  PlayerActionState legacy;
  legacy.type = humanoid->GetCurrentFunctionType();
  legacy.frame = humanoid->GetFrameNum();
  legacy.frameCount = humanoid->GetFrameCount();
  legacy.elapsedTime_ms = legacy.frame * 10;
  legacy.durationTime_ms = legacy.frameCount * 10;
  const Anim *anim = humanoid->GetCurrentAnim();
  legacy.contactFrame = anim->touchFrame;
  legacy.contactTime_ms =
      anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  legacy.contactPosition = anim->touchPos;
  return legacy;
}

void PlayerBase::CheckSimulationActionOracle() const {
  const PlayerActionState legacy = CaptureLegacyActionState();
  const bool contactPositionMatches =
      std::memcmp(actionState.contactPosition.coords,
                  legacy.contactPosition.coords,
                  sizeof(actionState.contactPosition.coords)) == 0;
  std::string mismatch;
  if (actionState.type != legacy.type) {
    mismatch = "type";
  } else if (actionState.frame != legacy.frame) {
    mismatch = "frame";
  } else if (actionState.frameCount != legacy.frameCount) {
    mismatch = "frame count";
  } else if (actionState.elapsedTime_ms != legacy.elapsedTime_ms) {
    mismatch = "elapsed time";
  } else if (actionState.durationTime_ms != legacy.durationTime_ms) {
    mismatch = "duration";
  } else if (actionState.contactTime_ms != legacy.contactTime_ms) {
    mismatch = "contact time";
  } else if (actionState.contactFrame != legacy.contactFrame) {
    mismatch = "contact frame";
  } else if (!contactPositionMatches) {
    mismatch = "contact position bits";
  }
  if (!mismatch.empty()) {
    Log(e_FatalError, "PlayerBase", "CheckSimulationActionOracle",
        "authoritative action state diverged from legacy oracle: " + mismatch +
            " (simulation frame=" + std::to_string(actionState.frame) +
            ", legacy frame=" + std::to_string(legacy.frame) +
            ", simulation elapsed=" +
            std::to_string(actionState.elapsedTime_ms) +
            ", legacy elapsed=" + std::to_string(legacy.elapsedTime_ms) + ")");
  }
}

void PlayerBase::BeginSimulationAction() {
  const Anim *anim = humanoid->GetCurrentAnim();
  PlayerActionDefinition definition;
  definition.type = humanoid->GetCurrentFunctionType();
  definition.durationTime_ms = humanoid->GetFrameCount() * 10;
  definition.contactTime_ms = anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  definition.contactPosition = anim->touchPos;
  PlayerActionExecutor::Begin(actionState, definition);
  PlayerActionExecutor::Begin(actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never
  // derive executor time from Humanoid.
  const int initialElapsedTime_ms = humanoid->GetFrameNum() * 10;
  if (initialElapsedTime_ms > 0) {
    PlayerActionExecutor::Step(actionState, initialElapsedTime_ms);
  }
  CheckSimulationActionOracle();
}

void PlayerBase::StepSimulationAction(int elapsedTime_ms) {
  PlayerActionExecutor::Step(actionState, elapsedTime_ms);
  CheckSimulationActionOracle();
}

bool PlayerBase::IsEligibleForProceduralLocomotion() const {
  return actionState.IsPureLocomotion(match->GetBallRetainer() == this);
}


void PlayerBase::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  humanoid->ResetPosition(newPos, focusPos);
  SynchronizeKinematicState();
  BeginSimulationAction();
}

void PlayerBase::OffsetPosition(const Vector3 &offset) {
  humanoid->OffsetPosition(offset);
  SynchronizeKinematicState();
  CheckSimulationActionOracle();
}

void PlayerBase::SetNextResetSituationAuditContext(int context) {
  resetSituationAuditContext = context;
}

void PlayerBase::Deactivate() {
  SetNextResetSituationAuditContext(kResetSituationBaseDeactivateSecond);
  ResetSituation(GetPosition());
  isActive = false;
  externalController = nullptr;
}

IController *PlayerBase::GetController() {
  if (ExternalControllerActive()) {
    return externalController->GetHumanController();
  } else {
    return controller.get();
  }
}

void PlayerBase::RequestCommand(PlayerCommandQueue &commandQueue) {
  if (controlIntent) {
    commandQueue = BuildPlayerCommands(*controlIntent, *this);
  } else if (ExternalControllerActive()) {
    externalController->GetHumanController()->RequestCommand(commandQueue);
  } else {
    controller->RequestCommand(commandQueue);
  }
}

void PlayerBase::SetExternalController(HumanGamer *externalController) {
  this->externalController = externalController;
  if (this->externalController) {
    this->externalController->GetHumanController()->Reset();
    this->externalController->GetHumanController()->SetPlayer(this);
  } else {
    controller->Reset();
  }
}

HumanController *PlayerBase::ExternalController() {
  return externalController ? externalController->GetHumanController() : nullptr;
}

bool PlayerBase::ExternalControllerActive() {
  return externalController && !externalController->GetHumanController()->Disabled();
}

void PlayerBase::Process() {
  if (isActive) {
    if (ExternalControllerActive()) externalController->GetHumanController()->Process(); else controller->Process();
    humanoid->Process();
    SynchronizeKinematicState();
    CheckSimulationActionOracle();
  }
}


float PlayerBase::GetStat(PlayerStat name) const {
  return playerData->GetStat(name);
}

float PlayerBase::GetMaxVelocity() const {
  // see humanoidbase's physics function
  return sprintVelocity * GetVelocityMultiplier();
}

float PlayerBase::GetVelocityMultiplier() const {
  // see humanoid_utils' physics function
  return 0.9f + playerData->get_physical_velocity() * 0.1f;
}

float PlayerBase::GetLastTouchBias(int decay_ms, unsigned long time_ms) {
  unsigned long adaptedTime_ms = time_ms;
  if (time_ms == 0) adaptedTime_ms = match->GetActualTime_ms();
  if (decay_ms > 0) return 1.0f - clamp((adaptedTime_ms - GetLastTouchTime_ms()) / (float)decay_ms, 0.0f, 1.0f);
  return 0.0f;
}

void PlayerBase::ResetSituation(const Vector3 &focusPos) {
  positionHistoryPerSecond.clear();
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  if (IsActive()) {
    // The reset itself is the discontinuity, so it anchors the generation that
    // a later reset re-entry compares against.
    resetDecisionGeneration = decisionLocomotionAuditGeneration;
    resetGenerationAnchorValid = true;
    lastResetSituation_ms = static_cast<int>(match->GetActualTime_ms());
    resetSinceLastPlayerTick = true;
    // A reset is also a continuity break, so any earlier intent is invalid.
    ++decisionLocomotionState.continuityEpoch;
    if (resetSituationAuditContext == kResetSituationUnspecified) {
      resetSituationAuditContext = hasProcessedPlayerTick
          ? kResetSituationRuntime
          : kResetSituationInitialBeforeFirstPlayerTick;
    }
    lastResetSituationAuditContext = resetSituationAuditContext;
    humanoid->ResetSituation(focusPos);
    SynchronizeKinematicState();
    BeginSimulationAction();
  }
  if (GetController()) GetController()->Reset();
  resetSituationAuditContext = kResetSituationUnspecified;
}

void PlayerBase::ProcessStateBase(EnvState *state) {
  state->process(isActive);
  humanoid->ProcessState(state);
  kinematicState.ProcessState(state);
  // 4f-b1: the unconsumed kinematic locomotion shadow was removed. This
  // intentionally changes the save-state layout, not gameplay behavior.
  groundCollider.ProcessState(state);
  // A restored Humanoid spatial state, kinematic mirror and collider must
  // agree before any subsequent tick or reader can observe either.
  CheckSimulationKinematicOracle();
  actionState.ProcessState(state);
  // The continuity epoch decides whether a repair queries the controller, so it
  // is gameplay state and must survive save/load exactly. Its tracking booleans
  // belong to the same transition; the audit generation is deliberately excluded,
  // because telemetry must not change this contract.
  decisionLocomotionState.ProcessState(state);
  // c2b: the refresh clock decides when the controller is queried, so it is
  // gameplay state and must survive save/load exactly.
  locomotionIntentScheduler.ProcessState(state);
  playerDecisionQueue.ProcessState(state);
  playerDecisionScheduler.ProcessState(state);
  // After saving or loading the action state, require it to agree with the
  // Humanoid motion cursor before a subsequent tick can observe either.
  CheckSimulationActionOracle();
  if (IsActive()) {
    controller->ProcessState(state);
  }
  state->process(externalController);
  state->process(lastTouchTime_ms);
  state->process(lastTouchType);
  state->process(fatigueFactorInv);
  state->process(positionHistoryPerSecond);
}

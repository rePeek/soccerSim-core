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

#include "player.hpp"
#include "player_action_executor.hpp"
#include "player_locomotion.hpp"
#include "locomotion_intent_scheduler.hpp"
#include "legacy_locomotion_command.hpp"

#include <cstring>

#include "../match.hpp"

int &ReanchorPendingSet() { static int value = 0; return value; }
int &ReanchorConsumedBeforeRefresh() { static int value = 0; return value; }
int &ReanchorSupersededByRefresh() { static int value = 0; return value; }
std::vector<int> &ReanchorLifetime_ms() { static std::vector<int> values; return values; }
int &RestartCarriedCommandForward() { static int value = 0; return value; }
ReanchorResidency &ReanchorResidencyFor(int reason) {
  static ReanchorResidency records[kBeginReasonCount];
  return records[reason];
}

int &MaterialMovementReanchorsForBeginReason(int reason) {
  static int records[kBeginReasonCount] = {};
  return records[reason];
}

MaterialReanchorPreviousSource &
MaterialMovementReanchorPreviousSourceForBeginReason(int reason) {
  static MaterialReanchorPreviousSource records[kBeginReasonCount];
  return records[reason];
}

LifecycleOverrideConsumption &
LifecycleOverrideConsumptionForBeginReason(int reason) {
  static LifecycleOverrideConsumption records[kBeginReasonCount];
  return records[reason];
}

int &MaterialResetSituationReanchorsForContext(int context) {
  static int records[kResetSituationCallContextCount] = {};
  return records[context];
}

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
ResetSeedAuditEpisode &ResetSeedLastClosedEpisode() {
  static ResetSeedAuditEpisode episode;
  return episode;
}
int &ResetSeedEpisodesRestartedBeforeCompletion() {
  static int value = 0;
  return value;
}
int &ResetSeedEpisodesStarted() { static int value = 0; return value; }
int &ResetSeedEpisodesConsumedBeforeDirect() { static int value = 0; return value; }
int &ResetSeedEpisodesDirectFirst() { static int value = 0; return value; }
int &ResetSeedForeignConsumeViolations() { static int value = 0; return value; }
std::vector<int> &ResetSeedConsumeToDirectDelay_ms() {
  static std::vector<int> values;
  return values;
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
void PlayerBase::NoteLocomotionReentryTick(bool eligible, bool legacy_gate,
                                          bool scheduler_due, int now_ms) {
  const unsigned long long generation = decisionLocomotionState.generation;
  int category = 2;
  if (!reentryAuditStarted) {
    category = 0;
  } else if (eligible && wasPureLocomotionLastTick) {
    category = 1;
  } else if (eligible && resetSinceLastPlayerTick) {
    category = 3;
  }
  LocomotionReentryAudit &audit = LocomotionReentryAuditFor(category);
  if (category != 0) {
    ++audit.ticks;
    if (legacy_gate) ++audit.legacy_gate_true; else ++audit.legacy_gate_false;
    if (scheduler_due) ++audit.scheduler_due; else ++audit.scheduler_not_due;
    if (category >= 2) {
      if (generation != decisionGenerationAtLocomotionExit) {
        ++audit.generation_advanced;
      } else {
        ++audit.generation_unchanged;
        if (!legacy_gate) ++audit.unchanged_and_legacy_gate_false;
      }
    }
    if (lastDirectMovementIntentPublication_ms >= 0) {
      const int age_ms = now_ms - lastDirectMovementIntentPublication_ms;
      audit.decision_age_sum_ms += age_ms;
      ++audit.decision_age_count;
      if (age_ms > audit.decision_age_max_ms) audit.decision_age_max_ms = age_ms;
    }
  }
  // Leaving procedural locomotion records the decision generation that was in
  // force, so a later re-entry can tell whether a fresh decision happened.
  if (!eligible && wasPureLocomotionLastTick) {
    decisionGenerationAtLocomotionExit = generation;
  }
  wasPureLocomotionLastTick = eligible;
  resetSinceLastPlayerTick = false;
  reentryAuditStarted = true;
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

const char *ActionBeginReasonName(int reason) {
  switch (reason) {
    case kBeginMovementToMovement: return "Movement->Movement";
    case kBeginOtherToMovement: return "Other->Movement";
    case kBeginMovementToOther: return "Movement->Other";
    case kBeginOtherToOther: return "Other->Other";
    case kBeginResetPosition: return "ResetPosition";
    case kBeginResetSituation: return "ResetSituation";
    case kBeginRetainSelection: return "RetainSelection";
    default: break;
  }
  return "unknown";
}

static int ResolveBeginReason(e_FunctionType previous_type, e_FunctionType new_type) {
  if (new_type == e_FunctionType_Movement) {
    return previous_type == e_FunctionType_Movement ? kBeginMovementToMovement
                                                    : kBeginOtherToMovement;
  }
  return previous_type == e_FunctionType_Movement ? kBeginMovementToOther
                                                  : kBeginOtherToOther;
}

void PlayerBase::CloseReanchorEpisode() {
  DO_VALIDATION;
  if (!episode_active) return;
  ReanchorResidency &record = ReanchorResidencyFor(episode_active_reason);
  ++record.n;
  record.lifetime_ms.push_back(static_cast<int>(match->GetActualTime_ms()) - episode_start_ms);
  record.ticks.push_back(episode_ticks);
  episode_active = false;
}
int &RestartConstructedCommand() { static int value = 0; return value; }
int &RetainCarriedCommandForward() { static int value = 0; return value; }
int &RetainConstructedCommand() { static int value = 0; return value; }

#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"

#include "../../main.hpp"
#include "../../utils.hpp"

#include "../../base/geometry/triangle.hpp"

namespace {

// Bit comparison, not an epsilon: a movement mirror that is only approximately
// right is a synchronization bug, and masking it would let a later procedural
// producer inherit an inconsistent baseline.
bool Vector3BitsEqual(const Vector3 &a, const Vector3 &b) {
  DO_VALIDATION;
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

bool FloatBitsEqual(float a, float b) {
  DO_VALIDATION;
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

PlayerBase::PlayerBase(Match *match, PlayerData *playerData)
    : match(match),
      playerData(playerData),
      stable_id(GetContext().stablePlayerCount++) {
  DO_VALIDATION;
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  fatigueFactorInv = 1.0;
}

PlayerBase::~PlayerBase() {
  DO_VALIDATION;
  if (isActive) Deactivate();
}

bool PlayerBase::IsKinematicMirrorConsistent() const {
  DO_VALIDATION;
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
  DO_VALIDATION;
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
  DO_VALIDATION;
  humanoid->Mirror();
  kinematicState.Mirror();
  kinematicShadow.Mirror();
  groundCollider.Mirror();
  CheckSimulationKinematicOracle();
}

void PlayerBase::SynchronizeKinematicState() {
  DO_VALIDATION;
  kinematicState.position = humanoid->GetPosition();
  kinematicState.velocity = humanoid->GetMovement();
  kinematicState.facing = humanoid->GetDirectionVec();
  kinematicState.bodyFacing = humanoid->GetBodyDirectionVec();
  kinematicState.speed = kinematicState.velocity.GetLength();
  groundCollider.SetCenter(kinematicState.position);
  CheckSimulationKinematicOracle();
}
// The last command actually consumed by procedural locomotion.
void PlayerBase::SetSimulationMovementCommand(const PlayerCommand &command) {
  DO_VALIDATION;
  SetSimulationMovementCommand(
      command, LocomotionCommandSource::ActionCoupledIntent);
}

void PlayerBase::SetSimulationMovementCommand(
    const PlayerCommand &command, LocomotionCommandSource source) {
  DO_VALIDATION;
  if (!command.useDesiredMovement) return;
  // c2b authority rule. Movement intent has exactly one producer: the
  // simulation's own cadence. Legacy acceptance may still seed the state while
  // nothing has been established yet (activation, reset, state restore), but it
  // may never overwrite an established Movement intent, and every such attempt
  // is counted so the migration can prove the animation lifecycle stopped being
  // a producer.
  const bool simulation_owned_movement_source =
      source == LocomotionCommandSource::DirectMovementIntent ||
      source == LocomotionCommandSource::SimulationFallbackIntent;
  if (command.desiredFunctionType == e_FunctionType_Movement &&
      !simulation_owned_movement_source && movementCommandState.initialized) {
    ++LegacyMovementOverwriteAttempts();
    return;
  }
  movementCommandState.command = command;
  movementCommandState.source = source;
  movementCommandState.initialized = true;
  switch (source) {
    case LocomotionCommandSource::DirectMovementIntent:
      ++PlayerMovementCommandDirectAdoptions();
      lastDirectMovementIntentPublication_ms =
          static_cast<int>(match->GetActualTime_ms());
      // The decision-owned shadow is written here and nowhere else.
      decisionLocomotionState.command = command;
      decisionLocomotionState.initialized = true;
      ++decisionLocomotionState.generation;
      break;
    case LocomotionCommandSource::SimulationFallbackIntent:
      ++PlayerMovementCommandFallbackAdoptions();
      break;
    default:
      ++PlayerMovementCommandLegacyAdoptions();
      break;
  }
  if (source == LocomotionCommandSource::DirectMovementIntent) {
    CompleteLifecycleOverrideConsumptionAudit();
  }
  if (source == LocomotionCommandSource::SimulationSeed) {
    // A reset boundary re-seeds this player only. Another player's pending
    // episode must never be mistaken for this one.
    if (resetSeedAudit.active) ++ResetSeedEpisodesRestartedBeforeCompletion();
    resetSeedAudit = ResetSeedAuditEpisode();
    resetSeedAudit.active = true;
    resetSeedAudit.context = lastResetSituationAuditContext;
    resetSeedAudit.started_ms = static_cast<int>(match->GetActualTime_ms());
    ++ResetSeedEpisodesStarted();
  } else if (source == LocomotionCommandSource::DirectMovementIntent &&
             resetSeedAudit.active) {
    resetSeedAudit.direct_publish_ms =
        static_cast<int>(match->GetActualTime_ms());
    if (resetSeedAudit.locomotion_consumes == 0) {
      // The controller replaced the seed before locomotion ever saw it.
      ++ResetSeedEpisodesDirectFirst();
    } else {
      ++ResetSeedEpisodesConsumedBeforeDirect();
      ResetSeedConsumeToDirectDelay_ms().push_back(
          resetSeedAudit.direct_publish_ms - resetSeedAudit.first_consume_ms);
    }
    ResetSeedLastClosedEpisode() = resetSeedAudit;
    resetSeedAudit.active = false;
  }
}

bool PlayerBase::NoteLocomotionIntentCadence(bool legacy_opportunity) {
  DO_VALIDATION;
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

void PlayerBase::CommitLocomotionIntentRefresh() {
  if (movementCommandState.source ==
      LocomotionCommandSource::SimulationFallbackIntent) {
    ++PlayerPathLocalTripMovementFallbackRefreshCommits();
  }
  ++HumanoidIntentRefreshCommits();
  DO_VALIDATION;
  // How long a legacy re-anchor value stayed in force until the simulation
  // refreshed: the interval that decides whether replacing it needs a seed or
  // only a forced due tick.
  CloseReanchorEpisode();
  if (reanchorPendingSince_ms >= 0) {
    ReanchorLifetime_ms().push_back(static_cast<int>(match->GetActualTime_ms()) - reanchorPendingSince_ms);
    reanchorPendingSince_ms = -1;
  }
  // A direct refresh replaces any pending re-anchor before locomotion saw it.
  if (reanchorPendingConsumption) {
    ++ReanchorSupersededByRefresh();
    if (reanchorPendingSince_ms >= 0) {
      ReanchorLifetime_ms().push_back(static_cast<int>(match->GetActualTime_ms()) - reanchorPendingSince_ms);
      reanchorPendingSince_ms = -1;
    }
    reanchorPendingConsumption = false;
  }
  const float distance_to_ball =
      (match->GetBall()->Predict(0).Get2D() - kinematicState.position).GetLength();
  ++PlayerLocomotionIntentConsumedTicks();
  locomotionIntentScheduler.Schedule(
      static_cast<int>(match->GetActualTime_ms()),
      LocomotionIntentScheduler::CadenceForDistance_ms(
          distance_to_ball, match->GetBallRetainer() == this));
}

void PlayerBase::NoteLocomotionCommandConsumed() {
  if (episode_active) ++episode_ticks;
  DO_VALIDATION;
  NoteLifecycleOverrideConsumptionAudit();
  if (resetSeedAudit.active) {
    if (movementCommandState.source != LocomotionCommandSource::SimulationSeed) {
      ++resetSeedAudit.foreign_consumes;
      ++ResetSeedForeignConsumeViolations();
    } else {
      if (resetSeedAudit.locomotion_consumes == 0) {
        resetSeedAudit.first_consume_ms =
            static_cast<int>(match->GetActualTime_ms());
      }
      ++resetSeedAudit.locomotion_consumes;
    }
  }
  if (!reanchorPendingConsumption) return;
  ++ReanchorConsumedBeforeRefresh();
  reanchorPendingConsumption = false;
}

void PlayerBase::BeginLifecycleOverrideConsumptionAudit(int reason) {
  if (lifecycleOverrideAuditActive) {
    ++LifecycleOverrideConsumptionForBeginReason(
          lifecycleOverrideAuditReason).interrupted_by_lifecycle;
  }
  LifecycleOverrideConsumption &record =
      LifecycleOverrideConsumptionForBeginReason(reason);
  ++record.started;
  lifecycleOverrideAuditActive = true;
  lifecycleOverrideAuditReason = reason;
  lifecycleOverrideAuditStart_ms = static_cast<int>(match->GetActualTime_ms());
  lifecycleOverrideAuditTicks = 0;
  lifecycleOverrideAuditFirstConsumeDelay_ms = -1;
}

void PlayerBase::NoteLifecycleOverrideConsumptionAudit() {
  if (!lifecycleOverrideAuditActive) return;
  ++lifecycleOverrideAuditTicks;
  if (lifecycleOverrideAuditFirstConsumeDelay_ms < 0) {
    lifecycleOverrideAuditFirstConsumeDelay_ms =
        static_cast<int>(match->GetActualTime_ms()) -
        lifecycleOverrideAuditStart_ms;
  }
}

void PlayerBase::CompleteLifecycleOverrideConsumptionAudit() {
  if (!lifecycleOverrideAuditActive) return;
  LifecycleOverrideConsumption &record =
      LifecycleOverrideConsumptionForBeginReason(lifecycleOverrideAuditReason);
  ++record.completed_by_direct;
  record.next_direct_delay_ms.push_back(
      static_cast<int>(match->GetActualTime_ms()) - lifecycleOverrideAuditStart_ms);
  record.locomotion_ticks_before_direct.push_back(lifecycleOverrideAuditTicks);
  if (lifecycleOverrideAuditFirstConsumeDelay_ms >= 0) {
    ++record.consumed_before_direct;
    record.first_consume_delay_ms.push_back(
        lifecycleOverrideAuditFirstConsumeDelay_ms);
  }
  lifecycleOverrideAuditActive = false;
}

// 4b': intent refresh is held back purely because execution is not pure. This is
// the conflation under measurement: the scheduler is due, but the execution gate
// blocks the refresh.
bool PlayerBase::LocomotionIntentRefreshHeldIneligible() const {
  return locomotionIntentScheduler.Due(
             static_cast<int>(match->GetActualTime_ms())) &&
         !IsEligibleForProceduralLocomotion();
}
// 4b'-provenance: where did the command that a legacy Movement re-anchor
// republishes actually come from?
static int t4pv_due_ineligible = 0, t4pv_not_due = 0, t4pv_due_eligible = 0;
static int t4pv_candidate_present = 0, t4pv_candidate_absent = 0;
static long t4pv_age_sum = 0;
static int t4pv_age_max = -1, t4pv_age_count = 0, t4pv_no_query = 0;

void DumpReanchorProvenance() {
  printf("4pv-SUMMARY material_movement_reanchors: query_due+ineligible=%d not_due=%d due+eligible=%d\n",
         t4pv_due_ineligible, t4pv_not_due, t4pv_due_eligible);
  printf("4pv-SUMMARY query_candidate present=%d absent=%d | selected_age mean=%ld max=%d n=%d no_query=%d\n",
         t4pv_candidate_present, t4pv_candidate_absent,
         t4pv_age_count ? t4pv_age_sum / t4pv_age_count : 0, t4pv_age_max, t4pv_age_count,
         t4pv_no_query);
  fflush(stdout);
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
  DO_VALIDATION;
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

void PlayerBase::NoteMaterialMovementReanchor() {
  DO_VALIDATION;
  if (tr_last_query_ms < 0) {
    ++t4pv_no_query;
    return;
  }
  if (tr_last_query_due && !tr_last_query_eligible) ++t4pv_due_ineligible;
  else if (!tr_last_query_due) ++t4pv_not_due;
  else ++t4pv_due_eligible;
  if (tr_last_query_had_candidate) ++t4pv_candidate_present; else ++t4pv_candidate_absent;
  const int age = static_cast<int>(match->GetActualTime_ms()) - tr_last_query_ms;
  t4pv_age_sum += age;
  ++t4pv_age_count;
  if (age > t4pv_age_max) t4pv_age_max = age;
}
// Single place that turns a controller queue into a published locomotion intent.
// Returns true only when a Movement candidate with useDesiredMovement was
// published, so callers commit the scheduler refresh only on a real publication.
// Both Process() paths (real players and officials) share it.
bool PlayerBase::PublishMovementIntentFromQueue(const PlayerCommandQueue &queue) {
  DO_VALIDATION;
  for (const PlayerCommand &candidate : queue) {
    if (candidate.desiredFunctionType == e_FunctionType_Movement &&
        candidate.useDesiredMovement) {
      SetSimulationMovementCommand(candidate,
                                   LocomotionCommandSource::DirectMovementIntent);
      return true;
    }
  }
  return false;
}

void PlayerBase::CheckSimulationMovementCommandOracle() const {
  DO_VALIDATION;
  // Only the ticks that actually consume the command are compared. Other
  // actions legitimately keep their own non-Movement originatingCommand.
  if (!IsEligibleForProceduralLocomotion()) return;
  if (!movementCommandState.initialized) return;
  const PlayerCommand &live = humanoid->GetCurrentAnim()->originatingCommand;
  const PlayerCommand &shadow = movementCommandState.command;
  ++MovementOracleConsumesForSource(
      static_cast<int>(movementCommandState.source));
  // Source-dispatch policy. The oracle checks the locomotion state that the
  // world tick is about to consume, so its contract follows that state's
  // provenance instead of one animation-equality rule for every source.
  switch (movementCommandState.source) {
    case LocomotionCommandSource::DirectMovementIntent:
      // Simulation authoritative: structural validity is fatal, disagreement
      // with the animation is compatibility telemetry only.
      if (shadow.desiredFunctionType != e_FunctionType_Movement ||
          !shadow.useDesiredMovement) {
        Log(e_FatalError, "PlayerBase", "CheckSimulationMovementCommandOracle",
            "a direct locomotion intent must be a Movement command with "
            "useDesiredMovement");
      }
      if (MovementCommandDiffersMaterially(live, shadow)) {
        ++DirectVsLegacyCommandMateriallyDifferent();
      } else {
        ++DirectVsLegacyCommandEqual();
      }
      return;

    case LocomotionCommandSource::SimulationSeed:
      // Simulation authoritative and deliberately unlike the animation command.
      if (shadow.desiredFunctionType != e_FunctionType_Movement ||
          !shadow.useDesiredMovement ||
          !FloatBitsEqual(shadow.desiredVelocityFloat, 0.0f) ||
          shadow.useDesiredLookAt) {
        Log(e_FatalError, "PlayerBase", "CheckSimulationMovementCommandOracle",
            "a simulation reset seed must be a neutral zero-velocity Movement "
            "intent without a look target");
      }
      return;

    case LocomotionCommandSource::SimulationFallbackIntent:
    case LocomotionCommandSource::LegacyCarriedForwardIntent:
      // Both are defined as an exact projection of the animation command that
      // produced them, so the bit-exact mirror remains fatal for them.
      break;

    case LocomotionCommandSource::ActionCoupledIntent:
      // The locomotion payload arrived with an action command (BallControl /
      // Trap). The action axis may legitimately move on, so a stale animation
      // command is not proof that this locomotion state is invalid. Never
      // fatal: measure the disagreement instead.
      if (MovementCommandDiffersMaterially(live, shadow)) {
        ++ActionCoupledLegacyMismatch();
      }
      return;
  }
  // Exact legacy mirror for SimulationFallbackIntent and
  // LegacyCarriedForwardIntent.
  std::string mismatch;
  if (!Vector3BitsEqual(shadow.desiredDirection, live.desiredDirection)) {
    mismatch = "desiredDirection";
  } else if (!FloatBitsEqual(shadow.desiredVelocityFloat,
                             live.desiredVelocityFloat)) {
    mismatch = "desiredVelocityFloat";
  } else if (shadow.useDesiredMovement != live.useDesiredMovement) {
    mismatch = "useDesiredMovement";
  } else if (shadow.useDesiredLookAt != live.useDesiredLookAt) {
    mismatch = "useDesiredLookAt";
  } else if (shadow.desiredFunctionType != live.desiredFunctionType) {
    mismatch = "desiredFunctionType";
  } else if (shadow.useDesiredLookAt &&
             !Vector3BitsEqual(shadow.desiredLookAt, live.desiredLookAt)) {
    mismatch = "desiredLookAt";
  }
  if (!mismatch.empty()) {
    Log(e_FatalError, "PlayerBase", "CheckSimulationMovementCommandOracle",
        "the simulation movement command diverged from the legacy animation "
        "scheduler: " + mismatch);
  }
}

PlayerActionState PlayerBase::CaptureLegacyActionState() const {
  DO_VALIDATION;
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
  DO_VALIDATION;
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
  DO_VALIDATION;
  const Anim *anim = humanoid->GetCurrentAnim();
  PlayerActionDefinition definition;
  definition.type = humanoid->GetCurrentFunctionType();
  definition.durationTime_ms = humanoid->GetFrameCount() * 10;
  definition.contactTime_ms = anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  definition.contactPosition = anim->touchPos;
  const e_FunctionType previous_action_type = actionState.type;
  const int resolved_begin_reason =
      nextActionBeginReason >= kBeginResetPosition
          ? nextActionBeginReason
          : ResolveBeginReason(previous_action_type, definition.type);
  nextActionBeginReason = kBeginMovementToMovement;
  PlayerActionExecutor::Begin(actionState, definition);
  PlayerActionExecutor::Begin(actionState, definition);

  // Selection transitions now have a simulation-owned final Movement writer
  // after SelectAnim returns: DirectMovementIntent for controller output and
  // SimulationFallbackIntent for the local Trip fallback. Their raw legacy
  // re-anchor is therefore a dead intermediate write and is skipped below.
  //
  // ResetSituation remains the only explicit legacy lifecycle override: its
  // carried command is actually consumed before the next Direct refresh.
  // RetainSelection was never consumed and Direct overwrote it immediately, so
  // its raw write is also a dead intermediate and is skipped permanently.
  const bool lifecycle_rebuild =
      resolved_begin_reason != kBeginRetainSelection &&
      resolved_begin_reason >= kBeginResetPosition;
  if (anim->originatingCommand.useDesiredMovement &&
      (anim->originatingCommand.desiredFunctionType != e_FunctionType_Movement ||
       lifecycle_rebuild)) {
    if (anim->originatingCommand.desiredFunctionType ==
        e_FunctionType_Movement) {
      ++MovementCommandReanchors();
      if (MovementCommandDiffersMaterially(movementCommandState.command,
                                          anim->originatingCommand)) {
        ++MovementCommandReanchorsMateriallyDifferent();
        ++MaterialMovementReanchorsForBeginReason(resolved_begin_reason);
        if (resolved_begin_reason == kBeginResetSituation) {
          ++MaterialResetSituationReanchorsForContext(
              resetSituationAuditContext);
        }
        MaterialReanchorPreviousSource &previous_source =
            MaterialMovementReanchorPreviousSourceForBeginReason(
                resolved_begin_reason);
        switch (movementCommandState.source) {
          case LocomotionCommandSource::ActionCoupledIntent:
            ++previous_source.action_coupled;
            break;
          case LocomotionCommandSource::DirectMovementIntent:
            ++previous_source.direct;
            break;
          case LocomotionCommandSource::LegacyCarriedForwardIntent:
            ++previous_source.legacy_carried;
            break;
          case LocomotionCommandSource::SimulationSeed:
            ++previous_source.simulation_seed;
            break;
          case LocomotionCommandSource::SimulationFallbackIntent:
            ++previous_source.simulation_fallback;
            break;
        }
        if (resolved_begin_reason == kBeginResetSituation ||
            resolved_begin_reason == kBeginRetainSelection) {
          BeginLifecycleOverrideConsumptionAudit(resolved_begin_reason);
        }
        NoteMaterialMovementReanchor();
        reanchorPendingConsumption = true;
        CloseReanchorEpisode();
        episode_active = true;
        episode_active_reason = resolved_begin_reason;
        episode_start_ms = static_cast<int>(match->GetActualTime_ms());
        episode_ticks = 0;
        ++ReanchorPendingSet();
        reanchorPendingSince_ms = static_cast<int>(match->GetActualTime_ms());
      } else {
        ++MovementCommandReanchorsEqual();
      }
    } else {
      ++NonMovementCommandReanchors();
    }
    movementCommandState.command = anim->originatingCommand;
    movementCommandState.source = LocomotionCommandSource::LegacyCarriedForwardIntent;
    movementCommandState.initialized = true;
  }

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
  DO_VALIDATION;
  PlayerActionExecutor::Step(actionState, elapsedTime_ms);
  CheckSimulationActionOracle();
}

bool PlayerBase::IsEligibleForProceduralLocomotion() const {
  DO_VALIDATION;
  return actionState.IsPureLocomotion(match->GetBallRetainer() == this);
}

bool PlayerBase::HasSimulationLocomotionIntent() const {
  // PlayerCommand carries orthogonal action and locomotion components. A
  // BallControl/Trap action may still carry the valid movement payload consumed
  // by PlayerLocomotion, so desiredFunctionType is intentionally not a gate.
  return movementCommandState.initialized &&
         movementCommandState.command.useDesiredMovement;
}

void PlayerBase::ResetKinematicShadow() {
  DO_VALIDATION;
  kinematicShadow = kinematicState;
}

void PlayerBase::UpdateKinematicShadow() {
  DO_VALIDATION;
  const PlayerCommand &command = humanoid->GetOriginatingCommand();
  if (GetCurrentFunctionType() != e_FunctionType_Movement ||
      !command.useDesiredMovement) {
    ResetKinematicShadow();
    return;
  }

  // The single place where a legacy command becomes a locomotion input. The
  // authoritative execution path reuses this when the authority flips, so
  // prediction, execution and this shadow can never drift apart semantically.
  // Keep the adapter's entire input in simulation state. bodyFacing is
  // simulation-authoritative for pure locomotion and an exact legacy shadow
  // otherwise; it never needs a direct Humanoid read here.
  const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
      command, kinematicShadow, GetMaxVelocity(), kinematicShadow.bodyFacing);

  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = GetMaxVelocity();
  PlayerLocomotion::Step(kinematicShadow, input, parameters, 0.01f);
}

void PlayerBase::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  DO_VALIDATION;
  nextActionBeginReason = kBeginResetPosition;
  // H3e4f-g0b-3a provenance: the reset rebuilds spatial state; does it build a command?
  const PlayerCommand reset_command_before = humanoid->GetCurrentAnim()->originatingCommand;
  humanoid->ResetPosition(newPos, focusPos);
  if (Vector3BitsEqual(humanoid->GetCurrentAnim()->originatingCommand.desiredDirection, reset_command_before.desiredDirection) &&
      FloatBitsEqual(humanoid->GetCurrentAnim()->originatingCommand.desiredVelocityFloat, reset_command_before.desiredVelocityFloat)) {
    ++RestartCarriedCommandForward();
  } else {
    ++RestartConstructedCommand();
  }
  SynchronizeKinematicState();
  BeginSimulationAction();
  ResetKinematicShadow();
}

void PlayerBase::OffsetPosition(const Vector3 &offset) {
  DO_VALIDATION;
  humanoid->OffsetPosition(offset);
  SynchronizeKinematicState();
  CheckSimulationActionOracle();
  ResetKinematicShadow();
}

void PlayerBase::SetNextResetSituationAuditContext(int context) {
  resetSituationAuditContext = context;
}

void PlayerBase::Deactivate() {
  DO_VALIDATION;
  SetNextResetSituationAuditContext(kResetSituationBaseDeactivateSecond);
  ResetSituation(GetPosition());
  isActive = false;
  externalController = nullptr;
}

IController *PlayerBase::GetController() {
  DO_VALIDATION;
  if (ExternalControllerActive()) {
    return externalController->GetHumanController();
  } else {
    return controller.get();
  }
}

void PlayerBase::RequestCommand(PlayerCommandQueue &commandQueue) {
  DO_VALIDATION;
  if (ExternalControllerActive()) {
    externalController->GetHumanController()->RequestCommand(commandQueue);
  } else {
    controller->RequestCommand(commandQueue);
  }
}

void PlayerBase::SetExternalController(HumanGamer *externalController) {
  DO_VALIDATION;
  this->externalController = externalController;
  if (this->externalController) {
    DO_VALIDATION;
    this->externalController->GetHumanController()->Reset();
    this->externalController->GetHumanController()->SetPlayer(this);
  } else {
    controller->Reset();
  }
}

HumanController *PlayerBase::ExternalController() {
  DO_VALIDATION;
  return externalController ? externalController->GetHumanController() : nullptr;
}

bool PlayerBase::ExternalControllerActive() {
  DO_VALIDATION;
  return externalController && !externalController->GetHumanController()->Disabled();
}

void PlayerBase::Process() {
  DO_VALIDATION;
  if (isActive) {
    DO_VALIDATION;
    if (ExternalControllerActive()) externalController->GetHumanController()->Process(); else controller->Process();
    humanoid->Process();
    SynchronizeKinematicState();
    CheckSimulationActionOracle();
    UpdateKinematicShadow();
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
  DO_VALIDATION;
  unsigned long adaptedTime_ms = time_ms;
  if (time_ms == 0) adaptedTime_ms = match->GetActualTime_ms();
  if (decay_ms > 0) return 1.0f - clamp((adaptedTime_ms - GetLastTouchTime_ms()) / (float)decay_ms, 0.0f, 1.0f);
  return 0.0f;
}

void PlayerBase::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  positionHistoryPerSecond.clear();
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  if (IsActive()) {
    nextActionBeginReason = kBeginResetSituation;
    lastResetSituation_ms = static_cast<int>(match->GetActualTime_ms());
    resetSinceLastPlayerTick = true;
    if (resetSituationAuditContext == kResetSituationUnspecified) {
      resetSituationAuditContext = hasProcessedPlayerTick
          ? kResetSituationRuntime
          : kResetSituationInitialBeforeFirstPlayerTick;
    }
    lastResetSituationAuditContext = resetSituationAuditContext;
    humanoid->ResetSituation(focusPos);
    SynchronizeKinematicState();
    BeginSimulationAction();
    ResetKinematicShadow();
  }
  if (GetController()) GetController()->Reset();
  resetSituationAuditContext = kResetSituationUnspecified;
}

void PlayerBase::ProcessStateBase(EnvState *state) {
  DO_VALIDATION;
  state->process(isActive);
  humanoid->ProcessState(state);
  kinematicState.ProcessState(state);
  kinematicShadow.ProcessState(state);
  groundCollider.ProcessState(state);
  // A restored Humanoid spatial state, kinematic mirror and collider must
  // agree before any subsequent tick or reader can observe either.
  CheckSimulationKinematicOracle();
  actionState.ProcessState(state);
  movementCommandState.ProcessState(state);
  // c2b: the refresh clock decides when the controller is queried, so it is
  // gameplay state and must survive save/load exactly.
  locomotionIntentScheduler.ProcessState(state);
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

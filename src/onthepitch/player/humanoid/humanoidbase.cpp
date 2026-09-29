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
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include "humanoid.hpp"

#include "humanoid_utils.hpp"

#include "../playerbase.hpp"
#include "../legacy_locomotion_command.hpp"
#include "core/physics/movement/player/player_movement.hpp"
#include "core/physics/movement/player/player_movement.hpp"
#include "../../match.hpp"

#include "../../../main.hpp"

#include "../../AIsupport/AIfunctions.hpp"

#include "../../../utils/animationextensions/footballanimationextension.hpp"


constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle = 0.25f * pi;
constexpr float initialReQueueDelayFrames = 32;

const Vector3 allowedBodyDirVecs[] = {
    Vector3(0, -1, 0),
    Vector3(0, -1, 0).GetRotated2D(-0.25 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.25 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.75 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.75 * pi)
};

const radian allowedBodyDirAngles[] = {
        0 * pi,
     0.25 * pi,
    -0.25 * pi,
     0.75 * pi,
    -0.75 * pi
};

const Vector3 preferredDirectionVecs[] = {
    Vector3(0, -1, 0),
    Vector3(0, -1, 0).GetRotated2D(0.111 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.111 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.25 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.25 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.5 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.5 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.75 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.75 * pi),
    Vector3(0, -1, 0).GetRotated2D(0.999 * pi),
    Vector3(0, -1, 0).GetRotated2D(-0.999 * pi)
};

int &HumanoidProceduralMovementTicks() { static int value = 0; return value; }
int &HumanoidLegacyBodyPoseSamplesOnProceduralMovement() { static int value = 0; return value; }
int &HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement() { static int value = 0; return value; }
int &HumanoidLocomotionGateBothTrue() { static int value = 0; return value; }
int &HumanoidLocomotionGateLegacyOnly() { static int value = 0; return value; }
int &HumanoidLocomotionGateSimulationOnly() { static int value = 0; return value; }
int &HumanoidLocomotionGateBothFalse() { static int value = 0; return value; }

PlayerTickGateMismatchContext &PlayerTickGateMismatchFor(
    bool simulation_only) {
  static PlayerTickGateMismatchContext contexts[2];
  return contexts[simulation_only ? 1 : 0];
}

void RecordPlayerTickGateMismatch(
    bool legacy_gate, bool simulation_gate, bool initialized, int source,
    e_FunctionType command_type, bool command_uses_desired_movement,
    int direct_age_ms, int reset_age_ms) {
  if (legacy_gate == simulation_gate) return;
  PlayerTickGateMismatchContext &context =
      PlayerTickGateMismatchFor(simulation_gate);
  ++context.ticks;
  if (simulation_gate) {
    if (source >= 0 && source < 5) ++context.simulation_source[source];
  } else if (!initialized) {
    ++context.legacy_uninitialized;
  } else if (command_type != e_FunctionType_Movement) {
    ++context.legacy_non_movement;
  } else if (!command_uses_desired_movement) {
    ++context.legacy_movement_without_desired;
  } else {
    ++context.legacy_other;
  }
  if (direct_age_ms >= 0) {
    context.direct_age_sum_ms += direct_age_ms;
    ++context.direct_age_count;
    context.direct_age_max_ms = std::max(context.direct_age_max_ms, direct_age_ms);
  }
  if (reset_age_ms >= 0) {
    context.reset_age_sum_ms += reset_age_ms;
    ++context.reset_age_count;
    context.reset_age_max_ms = std::max(context.reset_age_max_ms, reset_age_ms);
  }
}
int &PlayerMovementCommandNonMovementTicks() { static int value = 0; return value; }

// 4b: one place that turns a controller queue into a published locomotion intent.
// Returns true only when a Movement candidate with useDesiredMovement was published,
// so callers can commit the scheduler refresh only on an actual publication.
int &PlayerMovementCommandDirectAdoptions() { static int value = 0; return value; }
int &PlayerLocomotionIntentDueTicks() { static int value = 0; return value; }
int &PlayerLocomotionCadenceSelectionSuppressed() { static int value = 0; return value; }
int &PlayerLocomotionIntentLegacyOpportunityTicks() { static int value = 0; return value; }
int &PlayerLocomotionIntentOverlapTicks() { static int value = 0; return value; }
int &PlayerLocomotionIntentDueIneligibleTicks() { static int value = 0; return value; }
int &PlayerLocomotionIntentConsumedTicks() { static int value = 0; return value; }
int &HumanoidEligibilityGainRefreshes() { static int value = 0; return value; }
int &HumanoidEligibilityGainCandidatesMissing() { static int value = 0; return value; }
int &PlayerPathControllerQueries() { static int value = 0; return value; }
int &PlayerPathQueriesWithMovement() { static int value = 0; return value; }
int &PlayerPathQueriesWithMovementSuppressedByRepair() { static int value = 0; return value; }
int &PlayerPathDirectPublications() { static int value = 0; return value; }
int &PlayerPathRefreshCommits() { static int value = 0; return value; }
int &PlayerPathLocalTripAttempts() { static int value = 0; return value; }
int &PlayerPathLocalTripSelected() { static int value = 0; return value; }
int &PlayerPathLocalTripMovementFallbackSelected() { static int value = 0; return value; }
int &PlayerPathLocalTripMovementFallbackRefreshCommits() { static int value = 0; return value; }
int &LegacyOnlyDecisionOpportunities() { static int value = 0; return value; }
int &LegacyOnlyDecisionQueries() { static int value = 0; return value; }
int &LegacyOnlyDecisionMovementQueries() { static int value = 0; return value; }
int &LegacyOnlyDecisionPublications() { static int value = 0; return value; }
int &LegacyOnlyDecisionMaterialChanges() { static int value = 0; return value; }
int &LegacyOnlyDecisionMovementSelectionPublications() { static int value = 0; return value; }
int &LegacyOnlyDecisionMovementSelectionMaterialChanges() { static int value = 0; return value; }
int &LegacyOnlyDecisionMovementSelections() { static int value = 0; return value; }
int &LegacyOnlyDecisionNonMovementSelections() { static int value = 0; return value; }
int &LegacyOnlyDecisionNoSelection() { static int value = 0; return value; }
int &LegacyOnlyDecisionCausedQueries() { static int value = 0; return value; }
int &SimulationDecisionCachePresent() { static int value = 0; return value; }
int &SimulationDecisionCacheMissing() { static int value = 0; return value; }
std::vector<int> &SimulationDecisionCacheAge_ms() { static std::vector<int> values; return values; }
int &SimulationDecisionLiveHasMovement() { static int value = 0; return value; }
int &SimulationDecisionCacheHasMovement() { static int value = 0; return value; }
int &SimulationDecisionMovementEqual() { static int value = 0; return value; }
int &SimulationDecisionMovementDifferent() { static int value = 0; return value; }
int &SimulationDecisionQueueIdentical() { static int value = 0; return value; }
std::vector<int> &SimulationDecisionFirstDiffIndex() { static std::vector<int> values; return values; }
int &SimulationDecisionProofMovementProven() { static int value = 0; return value; }
int &SimulationDecisionProofMovementUnproven() { static int value = 0; return value; }
int &SimulationDecisionProofActionProven() { static int value = 0; return value; }
int &SimulationDecisionProofActionUnproven() { static int value = 0; return value; }
int &SimulationDecisionProofNoneProven() { static int value = 0; return value; }
int &SimulationDecisionProofNoneUnproven() { static int value = 0; return value; }
int &PlayerDecisionClockQueries() { static int value = 0; return value; }
int &PlayerDecisionClockPeriodicQueries() { static int value = 0; return value; }
int &PlayerDecisionClockForcedQueries() { static int value = 0; return value; }
int &PlayerDecisionClockQueueConsumersMissing() { static int value = 0; return value; }

namespace {

bool DecisionCommandFloatBitsEqual(float a, float b) {
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

bool DecisionCommandVectorBitsEqual(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

}  // namespace

// Field-for-field comparison of everything PlayerCommand::ProcessState()
// writes, which is the exact set a replayed SelectAnim would read. Floats and
// vectors compare bit-exact; the source of truth is ProcessState() itself, so a
// new serialized field has to be added here too.
bool PlayerCommandsDecisionEqual(const PlayerCommand &a, const PlayerCommand &b) {
  if (a.desiredFunctionType != b.desiredFunctionType) return false;
  if (a.useDesiredMovement != b.useDesiredMovement) return false;
  if (!DecisionCommandVectorBitsEqual(a.desiredDirection, b.desiredDirection)) return false;
  if (a.strictMovement != b.strictMovement) return false;
  if (!DecisionCommandFloatBitsEqual(a.desiredVelocityFloat, b.desiredVelocityFloat)) return false;
  if (a.useDesiredLookAt != b.useDesiredLookAt) return false;
  if (!DecisionCommandVectorBitsEqual(a.desiredLookAt, b.desiredLookAt)) return false;
  if (a.useTouchInfo != b.useTouchInfo) return false;
  if (!DecisionCommandVectorBitsEqual(a.touchInfo.inputDirection, b.touchInfo.inputDirection)) return false;
  if (!DecisionCommandFloatBitsEqual(a.touchInfo.inputPower, b.touchInfo.inputPower)) return false;
  if (!DecisionCommandFloatBitsEqual(a.touchInfo.autoDirectionBias, b.touchInfo.autoDirectionBias)) return false;
  if (!DecisionCommandFloatBitsEqual(a.touchInfo.autoPowerBias, b.touchInfo.autoPowerBias)) return false;
  if (!DecisionCommandVectorBitsEqual(a.touchInfo.desiredDirection, b.touchInfo.desiredDirection)) return false;
  if (!DecisionCommandFloatBitsEqual(a.touchInfo.desiredPower, b.touchInfo.desiredPower)) return false;
  if (a.touchInfo.targetPlayer != b.touchInfo.targetPlayer) return false;
  if (a.touchInfo.forcedTargetPlayer != b.touchInfo.forcedTargetPlayer) return false;
  if (a.onlyDeflectAnimsThatPickupBall != b.onlyDeflectAnimsThatPickupBall) return false;
  if (a.useTripType != b.useTripType) return false;
  if (a.tripType != b.tripType) return false;
  if (a.useDesiredTripDirection != b.useDesiredTripDirection) return false;
  if (!DecisionCommandVectorBitsEqual(a.desiredTripDirection, b.desiredTripDirection)) return false;
  if (a.useSpecialVar1 != b.useSpecialVar1) return false;
  if (a.specialVar1 != b.specialVar1) return false;
  if (a.useSpecialVar2 != b.useSpecialVar2) return false;
  if (a.specialVar2 != b.specialVar2) return false;
  if (a.modifier != b.modifier) return false;
  return true;
}
int &PlayerPathCandidatesMissing() { static int value = 0; return value; }
int &HumanoidBasePathRefreshCommits() { static int value = 0; return value; }
int &HeldDueMovementRetainsTicks() { static int value = 0; return value; }
int &HeldDueMovementRetainsCandidate() { static int value = 0; return value; }
int &HeldDueBallControlTicks() { static int value = 0; return value; }
int &HeldDueBallControlCandidate() { static int value = 0; return value; }
int &HeldDueTrapTicks() { static int value = 0; return value; }
int &HeldDueTrapCandidate() { static int value = 0; return value; }
int &HeldDueOtherTicks() { static int value = 0; return value; }
int &HeldDueOtherCandidate() { static int value = 0; return value; }
int &HumanoidIntentRefreshes() { static int value = 0; return value; }
int &HumanoidIntentCandidatesMissing() { static int value = 0; return value; }
int &HumanoidIntentRefreshCommits() { static int value = 0; return value; }
int &DirectVsLegacyCommandEqual() { static int value = 0; return value; }
int &DirectVsLegacyCommandMateriallyDifferent() { static int value = 0; return value; }

int &HumanoidSchedulerQueries() { static int value = 0; return value; }
int &HumanoidMaterialCommandCandidates() { static int value = 0; return value; }
int &HumanoidMaterialCommandsAccepted() { static int value = 0; return value; }
int &HumanoidMovementSelections() { static int value = 0; return value; }
int &HumanoidMovementSwitches() { static int value = 0; return value; }
int &HumanoidMovementRequeues() { static int value = 0; return value; }
int &HumanoidMovementFromOtherAction() { static int value = 0; return value; }
std::vector<int> &HumanoidMovementCommandLifetimes_ms() {
  static std::vector<int> values;
  return values;
}
std::vector<int> &HumanoidSelectedMovementFrames() {
  static std::vector<int> values;
  return values;
}

bool MovementCommandDiffersMaterially(const PlayerCommand &in_force,
                                      const PlayerCommand &candidate) {
  return in_force.desiredFunctionType != candidate.desiredFunctionType ||
         ((in_force.desiredDirection * in_force.desiredVelocityFloat) -
          (candidate.desiredDirection * candidate.desiredVelocityFloat))
                 .GetLength() >= 1.5f;
}

namespace {
struct PlayerDecisionQueryHistory {
  bool initialized = false;
  int time_ms = -1;
  PlayerCommandQueue commands;
};
struct PlayerDecisionCadenceMetrics {
  std::vector<int> intervals[5];  // all, legacy, locomotion, repair, decision clock
  std::vector<int> action_intervals[4];  // movement, ballcontrol, trap, other
  std::vector<int> nonmovement_appearance_gaps;
  int movement_changed = 0;
  int nonmovement_appeared = 0;
  int nonmovement_disappeared = 0;
  int nonmovement_changed = 0;
  int queue_identical = 0;
};
std::unordered_map<const void *, PlayerDecisionQueryHistory> &DecisionQueryHistory() {
  static std::unordered_map<const void *, PlayerDecisionQueryHistory> histories;
  return histories;
}
PlayerDecisionCadenceMetrics &DecisionCadenceMetrics() {
  static PlayerDecisionCadenceMetrics metrics;
  return metrics;
}
int DecisionActionBucket(e_FunctionType type) {
  if (type == e_FunctionType_Movement) return 0;
  if (type == e_FunctionType_BallControl) return 1;
  if (type == e_FunctionType_Trap) return 2;
  return 3;
}
int DecisionCauseBucket(PlayerDecisionQueryCause cause) {
  switch (cause) {
    case PlayerDecisionQueryCause::LegacyCaused: return 1;
    case PlayerDecisionQueryCause::LocomotionCadence: return 2;
    case PlayerDecisionQueryCause::ContinuityRepair: return 3;
    case PlayerDecisionQueryCause::PlayerDecisionClockPeriodic: return 4;
  }
  return 0;
}
bool SameNonMovementCandidates(const PlayerCommandQueue &a,
                               const PlayerCommandQueue &b) {
  std::vector<const PlayerCommand *> left;
  std::vector<const PlayerCommand *> right;
  for (const PlayerCommand &command : a)
    if (command.desiredFunctionType != e_FunctionType_Movement) left.push_back(&command);
  for (const PlayerCommand &command : b)
    if (command.desiredFunctionType != e_FunctionType_Movement) right.push_back(&command);
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i)
    if (!PlayerCommandsDecisionEqual(*left[i], *right[i])) return false;
  return true;
}
const PlayerCommand *FirstMovementCandidate(const PlayerCommandQueue &queue) {
  for (const PlayerCommand &command : queue)
    if (command.desiredFunctionType == e_FunctionType_Movement && command.useDesiredMovement)
      return &command;
  return nullptr;
}
}  // namespace

void ResetPlayerDecisionCadenceTelemetry() {
  DecisionQueryHistory().clear();
  DecisionCadenceMetrics() = PlayerDecisionCadenceMetrics();
}

void RecordPlayerDecisionQuery(const void *player_key,
                               const PlayerCommandQueue &commands, int now_ms,
                               PlayerDecisionQueryCause cause,
                               e_FunctionType action_type) {
  PlayerDecisionQueryHistory &history = DecisionQueryHistory()[player_key];
  PlayerDecisionCadenceMetrics &metrics = DecisionCadenceMetrics();
  const int action_bucket = DecisionActionBucket(action_type);
  const int cause_bucket = DecisionCauseBucket(cause);
  if (history.initialized) {
    const int interval_ms = now_ms - history.time_ms;
    metrics.intervals[0].push_back(interval_ms);
    metrics.intervals[cause_bucket].push_back(interval_ms);
    metrics.action_intervals[action_bucket].push_back(interval_ms);
    const PlayerCommand *old_movement = FirstMovementCandidate(history.commands);
    const PlayerCommand *new_movement = FirstMovementCandidate(commands);
    if (!old_movement || !new_movement ||
        MovementCommandDiffersMaterially(*old_movement, *new_movement))
      ++metrics.movement_changed;
    bool old_nonmovement = false;
    bool new_nonmovement = false;
    for (const PlayerCommand &command : history.commands)
      old_nonmovement |= command.desiredFunctionType != e_FunctionType_Movement;
    for (const PlayerCommand &command : commands)
      new_nonmovement |= command.desiredFunctionType != e_FunctionType_Movement;
    if (!old_nonmovement && new_nonmovement) {
      ++metrics.nonmovement_appeared;
      metrics.nonmovement_appearance_gaps.push_back(interval_ms);
    } else if (old_nonmovement && !new_nonmovement) {
      ++metrics.nonmovement_disappeared;
    } else if (old_nonmovement && new_nonmovement &&
               !SameNonMovementCandidates(history.commands, commands)) {
      ++metrics.nonmovement_changed;
    }
    if (history.commands.size() == commands.size()) {
      bool identical = true;
      for (size_t i = 0; identical && i < commands.size(); ++i)
        identical = PlayerCommandsDecisionEqual(history.commands[i], commands[i]);
      if (identical) ++metrics.queue_identical;
    }
  }
  history.initialized = true;
  history.time_ms = now_ms;
  history.commands = commands;
}

void DumpPlayerDecisionCadenceTelemetry() {
  const PlayerDecisionCadenceMetrics &metrics = DecisionCadenceMetrics();
  const auto percentile = [](std::vector<int> values, double fraction) {
    if (values.empty()) return -1;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(fraction * (values.size() - 1))];
  };
  const char *cause_names[] = {"all", "legacy", "locomotion_cadence",
                               "continuity_repair", "player_decision_clock"};
  const char *action_names[] = {"movement", "ballcontrol", "trap", "other"};
  for (int i = 0; i < 5; ++i) {
    const std::vector<int> &values = i == 0 ? metrics.intervals[0] : metrics.intervals[i];
    std::cout << "  decision_cadence cause=" << cause_names[i]
              << " n=" << values.size() << " p50=" << percentile(values, 0.50)
              << " p90=" << percentile(values, 0.90)
              << " p99=" << percentile(values, 0.99)
              << " max=" << percentile(values, 1.0) << " ms\n";
  }
  for (int i = 0; i < 4; ++i) {
    const std::vector<int> &values = metrics.action_intervals[i];
    std::cout << "  decision_cadence action=" << action_names[i]
              << " n=" << values.size() << " p50=" << percentile(values, 0.50)
              << " p90=" << percentile(values, 0.90)
              << " p99=" << percentile(values, 0.99)
              << " max=" << percentile(values, 1.0) << " ms\n";
  }
  std::cout << "  decision_queue_churn movement_changed=" << metrics.movement_changed
            << " nonmovement_appeared=" << metrics.nonmovement_appeared
            << " nonmovement_disappeared=" << metrics.nonmovement_disappeared
            << " nonmovement_changed=" << metrics.nonmovement_changed
            << " queue_identical=" << metrics.queue_identical << "\n";
  const std::vector<int> &gaps = metrics.nonmovement_appearance_gaps;
  std::cout << "  decision_queue_churn nonmovement_appearance_gap_ms n=" << gaps.size()
            << " p50=" << percentile(gaps, 0.50) << " p90=" << percentile(gaps, 0.90)
            << " p99=" << percentile(gaps, 0.99) << " max=" << percentile(gaps, 1.0)
            << "\n";
}

bool RecordSchedulerQuery(const PlayerCommand &in_force,
                          const PlayerCommand &candidate) {
  ++HumanoidSchedulerQueries();
  const bool material = MovementCommandDiffersMaterially(in_force, candidate);
  if (material) ++HumanoidMaterialCommandCandidates();
  return material;
}

void RecordMovementCommandAcceptance(bool material_candidate,
                                     e_FunctionType previous_type,
                                     int previous_elapsed_ms,
                                     e_InterruptAnim interrupt,
                                     const PlayerCommand &command,
                                     const Animation *anim) {
  if (command.desiredFunctionType != e_FunctionType_Movement) return;
  if (material_candidate) ++HumanoidMaterialCommandsAccepted();
  ++HumanoidMovementSelections();
  HumanoidSelectedMovementFrames().push_back(anim->GetFrameCount());
  if (previous_type == e_FunctionType_Movement) {
    // How long the movement command being replaced had been in force. This is
    // exactly the duration the animation lifecycle currently decides.
    HumanoidMovementCommandLifetimes_ms().push_back(previous_elapsed_ms);
  }
  if (previous_type != e_FunctionType_Movement) {
    ++HumanoidMovementFromOtherAction();
  } else if (interrupt == e_InterruptAnim_ReQueue) {
    ++HumanoidMovementRequeues();
  } else {
    ++HumanoidMovementSwitches();
  }
}


MovementAnimationPerturbation &MovementAnimationPerturbationAudit() {
  static MovementAnimationPerturbation value;
  return value;
}

bool &ContactAuthorityAuditEnabled() {
  static bool enabled = false;
  return enabled;
}

bool IsTrackedScheduledContact(e_FunctionType type) {
  return type == e_FunctionType_Shot ||
         type == e_FunctionType_ShortPass ||
         type == e_FunctionType_LongPass ||
         type == e_FunctionType_HighPass ||
         type == e_FunctionType_Trap ||
         type == e_FunctionType_BallControl;
}

ContactAuthorityAudit &ContactAuthorityFor(e_FunctionType type) {
  static ContactAuthorityAudit records[6];
  switch (type) {
    case e_FunctionType_Shot: return records[0];
    case e_FunctionType_ShortPass: return records[1];
    case e_FunctionType_LongPass: return records[2];
    case e_FunctionType_HighPass: return records[3];
    case e_FunctionType_Trap: return records[4];
    case e_FunctionType_BallControl: return records[5];
    default:
      Log(e_FatalError, "ContactAuthorityAudit", "ContactAuthorityFor",
          "untracked scheduled contact type");
      return records[0];
  }
}

int &HumanoidFootCounterfactualSelections() { static int value = 0; return value; }
int &HumanoidFootWinnerChanged() { static int value = 0; return value; }
int &HumanoidFootFrameCountDiff() { static int value = 0; return value; }
int &HumanoidFootQuadrantDiff() { static int value = 0; return value; }
int &HumanoidFootOutgoingVelocityDiff() { static int value = 0; return value; }
int &HumanoidFootOutgoingAngleBitsDiff() { static int value = 0; return value; }
int &HumanoidFootOutgoingAngleBucketDiff() { static int value = 0; return value; }
int &HumanoidFootSpecialStateDiff() { static int value = 0; return value; }
int &HumanoidFootLifecycleChanged() { static int value = 0; return value; }

void RecordFootCounterfactual(int with_foot_head, int without_foot_head,
                              AnimCollection *anims) {
  ++HumanoidFootCounterfactualSelections();
  if (with_foot_head == without_foot_head) return;
  ++HumanoidFootWinnerChanged();
  Animation *with_foot = anims->GetAnim(with_foot_head);
  Animation *without_foot = anims->GetAnim(without_foot_head);
  bool lifecycle_changed = false;
  if (with_foot->GetFrameCount() != without_foot->GetFrameCount()) {
    ++HumanoidFootFrameCountDiff();
    lifecycle_changed = true;
  }
  if (with_foot->GetVariableCache().quadrant_id() !=
      without_foot->GetVariableCache().quadrant_id()) {
    ++HumanoidFootQuadrantDiff();
    lifecycle_changed = true;
  }
  if (FloatToEnumVelocity(with_foot->GetOutgoingVelocity()) !=
      FloatToEnumVelocity(without_foot->GetOutgoingVelocity())) {
    ++HumanoidFootOutgoingVelocityDiff();
    lifecycle_changed = true;
  }
  const float with_angle = with_foot->GetOutgoingAngle();
  const float without_angle = without_foot->GetOutgoingAngle();
  if (with_angle != without_angle) ++HumanoidFootOutgoingAngleBitsDiff();
  // The requeue path buckets outgoing angles, so a different number only
  // matters when it lands in a different bucket. ~20 degrees, as an
  // approximation of the preferred-direction grid.
  const float bucket = pi / 9.0f;
  if (static_cast<int>(std::floor(with_angle / bucket + 0.5f)) !=
      static_cast<int>(std::floor(without_angle / bucket + 0.5f))) {
    ++HumanoidFootOutgoingAngleBucketDiff();
    lifecycle_changed = true;
  }
  if (with_foot->GetVariableCache().outgoing_special_state() !=
          without_foot->GetVariableCache().outgoing_special_state() ||
      with_foot->GetVariableCache().incoming_special_state() !=
          without_foot->GetVariableCache().incoming_special_state()) {
    ++HumanoidFootSpecialStateDiff();
    lifecycle_changed = true;
  }
  if (lifecycle_changed) ++HumanoidFootLifecycleChanged();
}

const radian preferredDirectionAngles[] = {
    0 * pi,
    0.111 * pi, // 20
    -0.111 * pi,
    0.25 * pi, // 45
    -0.25 * pi,
    0.5 * pi, // 90
    -0.5 * pi,
    0.75 * pi, // 135
    -0.75 * pi,
    0.999 * pi, // 180
    -0.999 * pi
};

HumanoidBase::HumanoidBase(PlayerBase *player, Match *match,
                           std::shared_ptr<AnimCollection> animCollection)
    : match(match),
      player(player),
      anims(animCollection) {
  DO_VALIDATION;
  interruptAnim = e_InterruptAnim_None;
  reQueueDelayFrames = 0;
  decayingPositionOffset = Vector3(0);
  decayingDifficultyFactor = 0.0f;

  assert(match);

  // Constructor runs before PlayerBase::humanoid has been installed. Preserve
  // legacy initialization; PlayerBase applies the reset result on activation.
  ResetPosition(LegacyResetKinematics::Evaluate(Vector3(0), Vector3(0)));
  mentalImageTime = 0;
}

HumanoidBase::~HumanoidBase() {}
void HumanoidBase::Mirror() {
  // Mirrors the field-level legacy contract, not a whole-state coordinate
  // transform: SpatialState::Mirror negates the movement fields but leaves the
  // facing/body fields untouched. PlayerBase::Mirror() mirrors its kinematic
  // mirror with exactly the same asymmetry, and the movement oracle verifies
  // the result bit-exactly.
  startPos.Mirror();
  startAngle.Mirror();
  nextStartPos.Mirror();
  nextStartAngle.Mirror();
  spatialState.Mirror();
  previousPosition2D.Mirror();
  tripDirection.Mirror();
  decayingPositionOffset.Mirror();
  predicate_RelDesiredDirection.Mirror();
  predicate_DesiredDirection.Mirror();
  predicate_RelIncomingBodyDirection.Mirror();
  predicate_LookAt.Mirror();
  predicate_RelDesiredTripDirection.Mirror();
  predicate_RelDesiredBallDirection.Mirror();
}

void HumanoidBase::Process() {
  DO_VALIDATION;

  decayingPositionOffset *= 0.95f;
  if (decayingPositionOffset.GetLength() < 0.005) decayingPositionOffset.Set(0);
  decayingDifficultyFactor = clamp(decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);

  assert(match);

  // See Humanoid::Process: the tick-start movement state is authoritative.
  const PlayerKinematicState tickStartState = player->GetKinematicState();

  CalculateSpatialState();
  spatialState.positionOffsetMovement = Vector3(0);
  ProjectMovementState(tickStartState);

  currentAnim.frameNum++;
  player->StepSimulationAction(10);
  const PlayerActionState &action = player->GetSimulationActionState();
  previousAnim_frameNum++;

  if (action.IsAtLastFrame() && interruptAnim == e_InterruptAnim_None) {
    DO_VALIDATION;
    interruptAnim = e_InterruptAnim_Switch;
  }

  bool mayReQueue = false;

  // already some anim interrupt waiting?

  if (mayReQueue) {
    DO_VALIDATION;
    if (interruptAnim != e_InterruptAnim_None) {
      DO_VALIDATION;
      mayReQueue = false;
    }
  }

  // okay, see if we need to requeue

  if (mayReQueue) {
    DO_VALIDATION;
    interruptAnim = e_InterruptAnim_ReQueue;
  }

  // H3e4f-c2b-2: the simulation owns when the controller is asked for a new
  // locomotion intent, and there is at most one query per tick. The two
  // schedules share one command queue, so queries equal the union of a due tick
  // and a legacy animation opportunity rather than the sum.
  const bool legacy_opportunity = interruptAnim != e_InterruptAnim_None;
  const bool simulation_due =
      player->NoteLocomotionIntentCadence(legacy_opportunity);
  if (legacy_opportunity || simulation_due) {
    DO_VALIDATION;

    PlayerCommandQueue commandQueue;
    // 4b''-prep: the controller's own queue is kept separate from what SelectAnim
    // consumes, because trip handling pre-fills commandQueue with locally generated
    // commands. Publishing a locally generated fallback as a DirectMovementIntent
    // would break provenance.
    PlayerCommandQueue controllerQueue;
    bool controller_queried = false;
    const auto EnsureControllerQuery = [&]() {
      if (controller_queried) return;
      player->RequestCommand(controllerQueue);
      controller_queried = true;
      for (const PlayerCommand &controller_command : controllerQueue) {
        commandQueue.push_back(controller_command);
      }
      {
        bool trq_has = false;
        for (const PlayerCommand &trq_c : controllerQueue) {
          if (trq_c.desiredFunctionType == e_FunctionType_Movement &&
              trq_c.useDesiredMovement) { trq_has = true; break; }
        }
        player->NoteControllerQuery(trq_has);
      }
    };
    const bool trip_local_queue =
        interruptAnim == e_InterruptAnim_Trip && tripType != 0;

    if (trip_local_queue) {
      DO_VALIDATION;
      AddTripCommandToQueue(commandQueue, tripDirection, tripType);
      tripType = 0;
      commandQueue.push_back(GetBasicMovementCommand(tripDirection, spatialState.floatVelocity)); // backup, if there's no applicable trip anim
    }

    // Simulation cadence: adopt fresh Movement intent without waiting for
    // SelectAnim to accept it. A locally generated trip queue is not a
    // controller intent, so the tick is left overdue instead of consumed.
    if (simulation_due && !trip_local_queue) {
      EnsureControllerQuery();
    }

    if (legacy_opportunity) {
    if (legacy_opportunity && player->LocomotionIntentRefreshHeldIneligible()) {
      bool held_has_candidate = false;
      for (const PlayerCommand &candidate : commandQueue) {
        if (candidate.desiredFunctionType == e_FunctionType_Movement &&
            candidate.useDesiredMovement) {
          held_has_candidate = true;
          break;
        }
      }
      const e_FunctionType held_type = player->GetCurrentFunctionType();
      const bool held_retains = match->GetBallRetainer() == player;
      if (held_type == e_FunctionType_Movement && held_retains) {
        ++HeldDueMovementRetainsTicks();
        if (held_has_candidate) ++HeldDueMovementRetainsCandidate();
      } else if (held_type == e_FunctionType_BallControl) {
        ++HeldDueBallControlTicks();
        if (held_has_candidate) ++HeldDueBallControlCandidate();
      } else if (held_type == e_FunctionType_Trap) {
        ++HeldDueTrapTicks();
        if (held_has_candidate) ++HeldDueTrapCandidate();
      } else {
        ++HeldDueOtherTicks();
        if (held_has_candidate) ++HeldDueOtherCandidate();
      }
    }
      EnsureControllerQuery();
    }

    // iterate through the command queue and pick the first that is applicable

    bool found = false;
    if (legacy_opportunity) {
      for (unsigned int i = 0; i < commandQueue.size(); i++) {
        DO_VALIDATION;

        const PlayerCommand &command = commandQueue[i];

        found = SelectAnim(command, interruptAnim);
        if (found) break;
      }
    }

    // 4b: the cadence check earlier in this tick saw the actor as non-pure, so a
    // due refresh was held back. SelectAnim has since turned the action into pure
    // Movement, i.e. eligibility went false -> true inside this decision phase.
    // Consume the held refresh here, reusing the queue this tick already queried;
    // never issue a second controller query.
    // 4b''-ordering: the single publication point for the tick. SelectAnim has
    // already run, so no legacy action-selection write can follow it and become
    // the final writer; the controller's Movement candidate is the last write.
    if (controller_queried) {
      if (player->PublishMovementIntentFromQueue(controllerQueue)) {
        ++HumanoidIntentRefreshes();
        player->CommitLocomotionIntentRefresh();
        ++HumanoidBasePathRefreshCommits();
      } else {
        ++HumanoidIntentCandidatesMissing();
      }
    }
    if (interruptAnim != e_InterruptAnim_ReQueue && !found) {
      DO_VALIDATION;
      printf("RED ALERT! NO APPLICABLE ANIM FOUND FOR HUMANOIDBASE! NOOOO!\n");
      printf("currentanimtype: %s\n", currentAnim.anim->GetVariable("type").c_str());
      for (unsigned int i = 0; i < commandQueue.size(); i++) {
        DO_VALIDATION;
        printf("desiredanimtype: %i\n", commandQueue[i].desiredFunctionType);
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
  reQueueDelayFrames = clamp(reQueueDelayFrames - 1, 0, 10000);

  interruptAnim = e_InterruptAnim_None;

  if (startPos.coords[2] != 0.f) {
    DO_VALIDATION;
    // the z coordinate not being 0 denotes something went horribly wrong :P
    Log(e_FatalError, "HumanoidBase", "Process", "BWAAAAAH FLYING PLAYERS!! height: " + real_to_str(startPos.coords[2]));
  }

  // movement/rotation smuggle

  // start with +1, because we want to influence the first frame as well
  // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
  // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
  float frameBias = (currentAnim.frameNum + 1) / (float)(currentAnim.anim->GetEffectiveFrameCount() + 1);

  // not sure if this is correct!
  // radian beginAngle = currentAnim.rotationSmuggle.begin;// * (1.0f - frameBias); // more influence in the beginning; act more like it was 0 later on. (yes, next one is a bias within a bias) *edit: disabled, looks better without
  // currentAnim.rotationSmuggleOffset = beginAngle * (1.0f - frameBias) +
  //                                      currentAnim.rotationSmuggle.end * frameBias;

  currentAnim.rotationSmuggleOffset = currentAnim.rotationSmuggle.begin * (1.0f - frameBias) +
                                       currentAnim.rotationSmuggle.end * frameBias;

  // next frame

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    DO_VALIDATION;
  } else {
  }

}

int HumanoidBase::GetIdleMovementAnimID() {
  DO_VALIDATION;
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;

  DataSet dataSet;
  anims->CrudeSelection(dataSet, query);

  SetIdlePredicate(1);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIdleVariable, this, _1, _2));

  SetIncomingBodyDirectionSimilarityPredicate(Vector3(0, -1, 0));
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIncomingBodyDirectionSimilarity, this, _1, _2));

  SetIncomingVelocitySimilarityPredicate(e_Velocity_Idle);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIncomingVelocitySimilarity, this, _1, _2));

  SetMovementSimilarityPredicate(Vector3(0, -1, 0), e_Velocity_Idle);
  SetBodyDirectionSimilarityPredicate(spatialState.position + Vector3(0, -10, 0).GetRotated2D(spatialState.angle)); // lookat
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareBodyDirectionSimilarity, this, _1, _2));

  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareMovementSimilarity, this, _1, _2));

  //printf("%s\n", anims->GetAnim(*dataSet.begin())->GetName().c_str());

  return *dataSet.begin();
}

PlayerResetKinematicEvaluation LegacyResetKinematics::Evaluate(
    const Vector3 &position, const Vector3 &focus) {
  DO_VALIDATION;
  PlayerResetKinematicEvaluation evaluation;
  evaluation.startAngle = FixAngle(
      (focus - position).GetNormalized(Vector3(0, -1, 0)).GetAngle2D());
  evaluation.kinematics.position = position;
  evaluation.kinematics.velocity = Vector3(0);
  evaluation.kinematics.movementFacing =
      Vector3(0, -1, 0).GetRotated2D(evaluation.startAngle);
  evaluation.kinematics.torsoFacing = Vector3(0, -1, 0);
  return evaluation;
}

void HumanoidBase::ResetPosition(
    const PlayerResetKinematicEvaluation &evaluation) {
  DO_VALIDATION;
  const PlayerKinematicResult &result = evaluation.kinematics;
  startPos = result.position;
  startAngle = evaluation.startAngle;
  nextStartPos = startPos;
  nextStartAngle = startAngle;
  previousPosition2D = startPos;

  // The four gameplay fields are a compatibility projection of the evaluated
  // reset result. Leave all other legacy reset/animation fields in place.
  spatialState.position = result.position;
  spatialState.angle = startAngle;
  spatialState.directionVec = result.movementFacing;
  spatialState.floatVelocity = 0;

  spatialState.actualMovement = Vector(0);
  spatialState.physicsMovement = Vector(0);
  spatialState.animMovement = Vector(0);
  spatialState.movement = result.velocity;
  spatialState.actionSmuggleMovement = Vector(0);
  spatialState.movementSmuggleMovement = Vector(0);
  spatialState.positionOffsetMovement = Vector(0);
  spatialState.relBodyAngleNonquantized = 0;

  spatialState.enumVelocity = e_Velocity_Idle;
  spatialState.floatVelocity = 0;
  spatialState.movement = result.velocity;
  spatialState.relBodyDirectionVec = Vector3(0, -1, 0);
  spatialState.relBodyAngle = 0;
  spatialState.bodyDirectionVec = result.torsoFacing;
  spatialState.bodyAngle = 0;
  spatialState.foot = e_Foot_Right;

  int idleAnimID = GetIdleMovementAnimID();
  currentAnim.id = idleAnimID;
  currentAnim.anim = anims->GetAnim(currentAnim.id);
  currentAnim.positions.clear();
  currentAnim.positions = match->GetAnimPositionCache(currentAnim.anim);
  currentAnim.frameNum =
      boostrandom(0, currentAnim.anim->GetEffectiveFrameCount() - 1);
  currentAnim.touchFrame = -1;
  currentAnim.originatingInterrupt = e_InterruptAnim_None;
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
  currentAnim.incomingMovement = Vector3(0);
  currentAnim.outgoingMovement = Vector3(0);
  currentAnim.positionOffset = Vector3(0);

  previousAnim_frameNum = 0;
  previousAnim_functionType = e_FunctionType_Movement;


  interruptAnim = e_InterruptAnim_None;
  tripType = 0;

  decayingPositionOffset = Vector3(0);
  decayingDifficultyFactor = 0.0f;

  reQueueDelayFrames = 0;
  tripDirection = Vector3(0);
}

void HumanoidBase::OffsetPosition(const Vector3 &offset) {
  DO_VALIDATION;

  assert(offset.coords[2] == 0.0f);

  // 7G-5: this is now a one-way compatibility projection. The authoritative
  // PlayerState is written by the caller (PlayerBase::OffsetPosition) before
  // this runs, so the legacy animation fields here follow the state instead of
  // driving it. SynchronizeKinematicState() is no longer called by collision
  // gameplay.
  nextStartPos += offset;
  startPos += offset;
  spatialState.position += offset;
  spatialState.positionOffsetMovement += offset * 100.0f;
  decayingPositionOffset += offset;
  if (decayingPositionOffset.GetLength() > 0.1f) decayingPositionOffset = decayingPositionOffset.GetNormalized() * 0.1f;
  currentAnim.positionOffset += offset;
}

void HumanoidBase::TripMe(const Vector3 &tripVector, int tripType) {
  DO_VALIDATION;
  if (match->GetBallRetainer() == player) return;
  const PlayerActionState &action = player->GetSimulationActionState();
  if (currentAnim.anim->GetVariableCache().incoming_special_state().compare(
          "") == 0 &&
      currentAnim.anim->GetVariableCache().outgoing_special_state().compare(
          "") == 0) {
    DO_VALIDATION;
    if (this->interruptAnim == e_InterruptAnim_None &&
        (action.type != e_FunctionType_Trip ||
         (currentAnim.anim->GetVariable("triptype").compare("1") == 0 &&
          tripType > 1)) &&
        action.type != e_FunctionType_Sliding) {
      DO_VALIDATION;
      this->interruptAnim = e_InterruptAnim_Trip;
      this->tripDirection = tripVector;
      this->tripType = tripType;
    }
  }
}

void HumanoidBase::ResetSituation(
    const PlayerResetKinematicEvaluation &evaluation) {
  DO_VALIDATION;
  mentalImageTime = 0;
  ResetPosition(evaluation);
}

bool HumanoidBase::_HighOrBouncyBall() const {
  float ballHeight1 = match->GetBall()->Predict(10).coords[2];
  float ballHeight2 = match->GetBall()->Predict(defaultTouchOffset_ms).coords[2];
  float ballBounce = std::fabs(match->GetBall()->GetMovement().coords[2]);
  bool highBall = false;
  if (ballHeight1 > 0.3f || ballHeight2 > 0.3f) {
    DO_VALIDATION;
    highBall = true;
  } else if (ballBounce > 1.0f) {
    DO_VALIDATION;  // low balls are also treated as 'high ball' when there's a
                    // lot of bounce going on (hard to control)
    highBall = true;
  }
  return highBall;
}

// ALERT: set sorting predicates before calling this function
void HumanoidBase::_KeepBestDirectionAnims(DataSet &dataSet,
                                           const PlayerCommand &command,
                                           bool strict, radian allowedAngle,
                                           int allowedVelocitySteps,
                                           int forcedQuadrantID) {
  DO_VALIDATION;

  assert(dataSet.size() != 0);

  int bestQuadrantID = forcedQuadrantID;
  if (bestQuadrantID == -1) {
    DO_VALIDATION;

    for (auto &anim : dataSet) {
      DO_VALIDATION;
      anims->GetAnim(anim)->order_float = GetMovementSimilarity(anim, predicate_RelDesiredDirection, predicate_DesiredVelocity, predicate_CorneringBias);
    }
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareByOrderFloat, this, _1, _2));

    // we want the best anim to be a baseanim, and compare other anims to it
    if (strict) {
      DO_VALIDATION;
      if (command.desiredFunctionType != e_FunctionType_Movement) {
        DO_VALIDATION;
        std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
      }
    }

    Animation *bestAnim = anims->GetAnim(*dataSet.begin());

    bestQuadrantID = bestAnim->GetVariableCache().quadrant_id();
  }

  const Quadrant &quadrant = anims->GetQuadrant(bestQuadrantID);

  DataSet::iterator iter = dataSet.begin();
  iter++;
  while (iter != dataSet.end()) {
    DO_VALIDATION;
    Animation *anim = anims->GetAnim(*iter);

    if (strict) {
      DO_VALIDATION;
      if (anim->GetVariableCache().quadrant_id() == bestQuadrantID) {
        DO_VALIDATION;
        iter++;
      } else {
        iter = dataSet.erase(iter);
      }
    } else {
      int quadrantID = anim->GetVariableCache().quadrant_id();
      const Quadrant &bestQuadrant = anims->GetQuadrant(bestQuadrantID);
      const Quadrant &quadrant = anims->GetQuadrant(quadrantID);

      bool predicate = true;

      if (!anim->GetVariableCache().lastditch()) {
        DO_VALIDATION;  // last ditch anims may always change velo
        if (std::abs(GetVelocityID(quadrant.velocity, true) - GetVelocityID(bestQuadrant.velocity, true)) > allowedVelocitySteps) predicate = false;
      }
      if (std::fabs(quadrant.angle - bestQuadrant.angle) > allowedAngle) predicate = false;

      if (predicate) {
        DO_VALIDATION;
        iter++;
      } else {
        iter = dataSet.erase(iter);
      }
    }
  }
}

// ALERT: set sorting predicates before calling this function
void HumanoidBase::_KeepBestBodyDirectionAnims(DataSet &dataSet,
                                               const PlayerCommand &command,
                                               bool strict,
                                               radian allowedAngle) {
  DO_VALIDATION;

  // delete nonqualified bodydir quadrants

  assert(dataSet.size() != 0);
  for (auto &anim : dataSet) {
    DO_VALIDATION;
    anims->GetAnim(anim)->order_float = DirectionSimilarityRating(anim);
  }
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareByOrderFloat, this, _1, _2));

  // we want the best anim to be a baseanim, and compare other anims to it
  if (strict) {
    DO_VALIDATION;
    if (command.desiredFunctionType != e_FunctionType_Movement) {
      DO_VALIDATION;
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
    }
  }

  Animation *bestAnim = anims->GetAnim(*dataSet.begin());

  radian bestOutgoingBodyAngle = ForceIntoAllowedBodyDirectionAngle(bestAnim->GetOutgoingBodyAngle());
  radian bestOutgoingAngle = ForceIntoPreferredDirectionAngle(bestAnim->GetOutgoingAngle());
  radian bestLookAngle = bestOutgoingBodyAngle + bestOutgoingAngle;

  DataSet::iterator iter = dataSet.begin();
  iter++;
  while (iter != dataSet.end()) {
    DO_VALIDATION;

    Animation *anim = anims->GetAnim(*iter);

    radian animOutgoingBodyAngle = ForceIntoAllowedBodyDirectionAngle(anim->GetOutgoingBodyAngle());
    radian animOutgoingAngle = ForceIntoPreferredDirectionAngle(anim->GetOutgoingAngle());
    radian animLookAngle = animOutgoingBodyAngle + animOutgoingAngle;

    float adaptedAllowedAngle = 0.06f * pi; // between 0 and 20 deg
    if (!strict) {
      DO_VALIDATION;
      adaptedAllowedAngle = allowedAngle;
    }
    if (std::fabs(animLookAngle - bestLookAngle) <= adaptedAllowedAngle) {
      DO_VALIDATION;
      iter++;
    } else {
      iter = dataSet.erase(iter);
    }
  }
}

bool HumanoidBase::SelectAnim(const PlayerCommand &command,
                              e_InterruptAnim localInterruptAnim,
                              bool preferPassAndShot) {
  DO_VALIDATION;  // returns false on no applicable anim found
  assert(command.desiredDirection.coords[2] == 0.0f);
  const PlayerActionState &action = player->GetSimulationActionState();
  const bool material_candidate =
      RecordSchedulerQuery(currentAnim.originatingCommand, command);

  if (localInterruptAnim != e_InterruptAnim_ReQueue || action.frame > 12)
    CalculateFactualSpatialState();

  // CREATE A CRUDE SET OF POTENTIAL ANIMATIONS

  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = command.desiredFunctionType;

  query.byFoot = false;
  query.foot = (spatialState.foot == e_Foot_Left) ? e_Foot_Right : e_Foot_Left;

  query.byIncomingVelocity = true;
  query.incomingVelocity = spatialState.enumVelocity;
  query.incomingVelocity_Strict = true;
  query.byIncomingBodyDirection = true;
  query.incomingBodyDirection_Strict = true;
  query.incomingBodyDirection = spatialState.relBodyDirectionVec;
  query.incomingVelocity_ForceLinearity = true;
  query.incomingBodyDirection_ForceLinearity = true;

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

  // NOW SORT OUT THE RESULTING SET

  float adaptedDesiredVelocityFloat = command.desiredVelocityFloat;

  if (command.useDesiredMovement) {
    DO_VALIDATION;

    Vector3 relDesiredDirection = command.desiredDirection.GetRotated2D(-spatialState.angle);
    SetMovementSimilarityPredicate(relDesiredDirection, FloatToEnumVelocity(adaptedDesiredVelocityFloat));
    SetBodyDirectionSimilarityPredicate(command.desiredLookAt);
    if (command.desiredFunctionType == e_FunctionType_Movement) {
      DO_VALIDATION;
      _KeepBestDirectionAnims(dataSet, command);
      if (command.useDesiredLookAt) _KeepBestBodyDirectionAnims(dataSet, command);
    }

    else {  // undefined animtype
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareMovementSimilarity, this, _1, _2));
    }
  }

  int desiredIdleLevel = 1;
  SetIdlePredicate(desiredIdleLevel);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareIdleVariable, this, _1, _2));

  SetFootSimilarityPredicate(spatialState.foot);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareFootSimilarity, this, spatialState.foot, _1, _2));

  SetIncomingBodyDirectionSimilarityPredicate(spatialState.relBodyDirectionVec);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareIncomingBodyDirectionSimilarity, this, _1, _2));

  SetIncomingVelocitySimilarityPredicate(spatialState.enumVelocity);
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareIncomingVelocitySimilarity, this, _1, _2));

  if (command.useDesiredTripDirection) {
    DO_VALIDATION;
    Vector3 relDesiredTripDirection = command.desiredTripDirection.GetRotated2D(-spatialState.angle);
    SetTripDirectionSimilarityPredicate(relDesiredTripDirection);
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareTripDirectionSimilarity, this, _1, _2));
  }

  if (command.desiredFunctionType != e_FunctionType_Movement) {
    DO_VALIDATION;
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareBaseanimSimilarity, this, _1, _2));
  }

  // process result

  int selectedAnimID = -1;
  std::vector<Vector3> positions_tmp;
  int touchFrame_tmp = -1;
  Vector3 touchPos_tmp;
  Vector3 actionSmuggle_tmp;
  radian rotationSmuggle_tmp = 0;

  if (dataSet.size() == 0) {
    DO_VALIDATION;
    return false;
  }
  if (command.desiredFunctionType == e_FunctionType_Movement ||
      command.desiredFunctionType == e_FunctionType_Trip ||
      command.desiredFunctionType == e_FunctionType_Special) {
    DO_VALIDATION;
    selectedAnimID = *dataSet.begin();
    Animation *nextAnim = anims->GetAnim(selectedAnimID);
    Vector3 desiredMovement = command.desiredDirection * command.desiredVelocityFloat;
    assert(desiredMovement.coords[2] == 0.0f);
    Vector3 desiredBodyDirectionRel = Vector3(0, -1, 0);
    if (command.useDesiredLookAt) desiredBodyDirectionRel = ((command.desiredLookAt - spatialState.position).Get2D().GetRotated2D(-spatialState.angle) - nextAnim->GetTranslation()).GetNormalized(Vector3(0, -1, 0));
    Vector3 physicsVector = CalculatePhysicsVector(nextAnim, command.useDesiredMovement, desiredMovement, command.useDesiredLookAt, desiredBodyDirectionRel, positions_tmp, rotationSmuggle_tmp);
  }

  // check if we really want to requeue - only requeue movement to movement, for example, when we want to go a different direction

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
         (FloatToEnumVelocity(currentAnim.anim->GetOutgoingVelocity()) ==
              e_Velocity_Idle &&
          std::fabs((ForceIntoPreferredDirectionAngle(
                    currentAnim.anim->GetOutgoingAngle()) -
                ForceIntoPreferredDirectionAngle(
                    anims->GetAnim(selectedAnimID)->GetOutgoingAngle()))) <
              0.20f * pi))) {
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
    currentAnim.functionType = command.desiredFunctionType;
    currentAnim.frameNum = 0;
    currentAnim.touchFrame = touchFrame_tmp;
    currentAnim.originatingInterrupt = localInterruptAnim;
    currentAnim.touchPos = touchPos_tmp;
    currentAnim.rotationSmuggle.begin = clamp(ModulateIntoRange(-pi, pi, spatialState.relBodyAngleNonquantized - currentAnim.anim->GetIncomingBodyAngle()) * bodyRotationSmoothingFactor, -bodyRotationSmoothingMaxAngle, bodyRotationSmoothingMaxAngle);
    currentAnim.rotationSmuggle.end = rotationSmuggle_tmp;
    currentAnim.rotationSmuggleOffset = 0;
    currentAnim.actionSmuggle = actionSmuggle_tmp;
    currentAnim.actionSmuggleOffset = Vector3(0);
    currentAnim.actionSmuggleSustain = Vector3(0);
    currentAnim.actionSmuggleSustainOffset = Vector3(0);
    currentAnim.movementSmuggle = Vector3(0);
    currentAnim.movementSmuggleOffset = Vector3(0);
    currentAnim.incomingMovement = spatialState.movement;
    currentAnim.outgoingMovement = CalculateOutgoingMovement(positions_tmp);
    currentAnim.positions.clear();
    currentAnim.positions.assign(positions_tmp.begin(), positions_tmp.end());
    currentAnim.positionOffset = 0.0;
    currentAnim.originatingCommand = command;
    RecordMovementCommandAcceptance(material_candidate, action.type,
                                    action.elapsedTime_ms, localInterruptAnim,
                                    command, currentAnim.anim);
    player->BeginSimulationAction();

    return true;
  }

  return false;
}

void HumanoidBase::CalculatePredictedSituation(Vector3 &predictedPos,
                                               radian &predictedAngle) {
  DO_VALIDATION;

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    DO_VALIDATION;
    assert(currentAnim.positions.size() > (unsigned int)currentAnim.anim->GetEffectiveFrameCount());
    predictedPos = spatialState.position + currentAnim.positions.at(currentAnim.anim->GetEffectiveFrameCount()) + currentAnim.actionSmuggle + currentAnim.actionSmuggleSustain + currentAnim.movementSmuggle;
  } else {
    predictedPos = spatialState.position + currentAnim.anim->GetTranslation().Get2D().GetRotated2D(spatialState.angle) + currentAnim.actionSmuggle + currentAnim.actionSmuggleSustain + currentAnim.movementSmuggle;
  }

  predictedAngle = spatialState.angle + currentAnim.anim->GetOutgoingAngle() + currentAnim.rotationSmuggle.end;
  predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);
  assert(predictedPos.coords[2] == 0.0f);
}

Vector3 HumanoidBase::CalculateOutgoingMovement(const std::vector<Vector3> &positions) const {
  if (positions.size() < 2) return 0;
  return (positions.at(positions.size() - 1) - positions.at(positions.size() - 2)) * 100.0f;
}

bool HumanoidBase::UsesProceduralLocomotion() const {
  // Execution authority is the Player Decision Clock's current-epoch intent, not
  // the animation command. Executability is the gate; the producer contract is a
  // separate fatal oracle.
  return player && player->IsEligibleForProceduralLocomotion() &&
         player->HasExecutableDecisionLocomotionIntent();
}


LegacyAnimationKinematicEvaluation LegacyAnimationKinematics::Evaluate(
    const HumanoidBase &humanoid) {
  DO_VALIDATION;
  LegacyAnimationKinematicEvaluation evaluation;
  const Anim &currentAnim = humanoid.currentAnim;
  const SpatialState &spatialState = humanoid.spatialState;

  Vector3 position;
  if (currentAnim.positions.size() >
      static_cast<unsigned int>(currentAnim.frameNum)) {
    position = humanoid.startPos + currentAnim.positions.at(currentAnim.frameNum) +
               currentAnim.actionSmuggleOffset +
               currentAnim.actionSmuggleSustainOffset +
               currentAnim.movementSmuggleOffset;
  } else {
    Quaternion orientation;
    currentAnim.anim->GetKeyFrame(BodyPart::player, currentAnim.frameNum,
                                  orientation, position);
    position.coords[2] = 0.0f;
    position = humanoid.startPos + position.GetRotated2D(humanoid.startAngle) +
               currentAnim.actionSmuggleOffset +
               currentAnim.actionSmuggleSustainOffset +
               currentAnim.movementSmuggleOffset;
  }

  evaluation.compatibility.updateFoot = currentAnim.frameNum > 12;
  if (evaluation.compatibility.updateFoot) {
    evaluation.compatibility.foot = currentAnim.anim->GetOutgoingFoot();
  }

  assert(humanoid.startPos.coords[2] == 0.0f);
  assert(currentAnim.actionSmuggleOffset.coords[2] == 0.0f);
  assert(currentAnim.movementSmuggleOffset.coords[2] == 0.0f);
  assert(position.coords[2] == 0.0f);

  evaluation.compatibility.actualMovement =
      (position - humanoid.previousPosition2D) * 100.0f;
  const float positionOffsetMovementIgnoreFactor = 0.5f;
  evaluation.compatibility.physicsMovement =
      evaluation.compatibility.actualMovement -
      spatialState.actionSmuggleMovement - spatialState.movementSmuggleMovement -
      spatialState.positionOffsetMovement * positionOffsetMovementIgnoreFactor;
  evaluation.compatibility.animMovement =
      evaluation.compatibility.physicsMovement;
  if (!currentAnim.positions.empty()) {
    // Preserve the legacy requeue measurement, including its deliberate
    // omission of smuggle and collision corrections.
    const std::vector<Vector3> &origPositionCache =
        humanoid.match->GetAnimPositionCache(currentAnim.anim);
    evaluation.compatibility.animMovement =
        CalculateMovementAtFrame(origPositionCache, currentAnim.frameNum, 1)
            .GetRotated2D(humanoid.startAngle);
  }

  const Vector3 movement = evaluation.compatibility.physicsMovement;
  const float floatVelocity = movement.GetLength();
  const e_Velocity enumVelocity = FloatToEnumVelocity(floatVelocity);

  ++HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement();
  Vector3 bodyPosition;
  Quaternion bodyOrientation;
  currentAnim.anim->GetKeyFrame(body, currentAnim.frameNum, bodyOrientation,
                                bodyPosition);
  real x, y, z;
  bodyOrientation.GetAngles(x, y, z);
  const Vector3 bodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(
      z + humanoid.startAngle + currentAnim.rotationSmuggleOffset);
  const Vector3 facing =
      enumVelocity != e_Velocity_Idle ? movement.GetNormalized()
                                     : bodyDirectionVec;
  const radian angle =
      ModulateIntoRange(-pi, pi, FixAngle(facing.GetAngle2D()));

  Vector3 relBodyDirectionVec;
  if (enumVelocity != e_Velocity_Idle) {
    Vector3 adaptedBodyDirectionVec = bodyDirectionVec.GetRotated2D(-angle);
    const bool preferCorrectVeloOverCorrectAngle = true;
    const radian bodyAngleRel =
        adaptedBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
    if (enumVelocity == e_Velocity_Sprint &&
        std::fabs(bodyAngleRel) >= 0.125f * pi) {
      if (preferCorrectVeloOverCorrectAngle) {
        adaptedBodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(
            0.12f * pi * signSide(bodyAngleRel));
      }
    } else if (enumVelocity == e_Velocity_Walk &&
               std::fabs(bodyAngleRel) >= 0.5f * pi) {
      if (preferCorrectVeloOverCorrectAngle) {
        adaptedBodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(
            0.495f * pi * signSide(bodyAngleRel));
      }
    }
    relBodyDirectionVec =
        humanoid.ForceIntoAllowedBodyDirectionVec(adaptedBodyDirectionVec);
  } else {
    relBodyDirectionVec = Vector3(0, -1, 0);
  }

  evaluation.kinematics.position = position;
  evaluation.kinematics.velocity = movement;
  evaluation.kinematics.movementFacing = facing;
  evaluation.kinematics.torsoFacing = relBodyDirectionVec.GetRotated2D(angle);
  return evaluation;
}

void HumanoidBase::ApplyLegacySpatialCompatibility(
    const LegacySpatialCompatibility &compatibility) {
  DO_VALIDATION;
  spatialState.actualMovement = compatibility.actualMovement;
  spatialState.physicsMovement = compatibility.physicsMovement;
  spatialState.animMovement = compatibility.animMovement;
  if (compatibility.updateFoot) spatialState.foot = compatibility.foot;
}

void HumanoidBase::CalculateSpatialState() {
  DO_VALIDATION;
  if (UsesProceduralLocomotion()) {
    // Root motion and body pose are not evaluated on the procedural path. Foot
    // remains animation-owned gait/selection compatibility data.
    if (currentAnim.frameNum > 12) {
      spatialState.foot = currentAnim.anim->GetOutgoingFoot();
    }
    ++HumanoidProceduralMovementTicks();
    return;
  }

  const LegacyAnimationKinematicEvaluation evaluation =
      LegacyAnimationKinematics::Evaluate(*this);
  ApplyLegacySpatialCompatibility(evaluation.compatibility);
  if (player) player->ApplyKinematicResult(evaluation.kinematics);
  ProjectPlayerStateToSpatialState(
      player ? player->GetKinematicState() : PlayerKinematicState{
          evaluation.kinematics.position, evaluation.kinematics.velocity,
          evaluation.kinematics.movementFacing, evaluation.kinematics.torsoFacing});
  if (player) player->CheckSimulationKinematicOracle();
}

void HumanoidBase::CalculateFactualSpatialState() {
  DO_VALIDATION;

  spatialState.foot = currentAnim.anim->GetOutgoingFoot();
  if (currentAnim.anim->GetVariableCache().outgoing_special_state().empty()) {
    return;
  }

  PlayerKinematicResult result;
  if (player) {
    const PlayerKinematicState &state = player->GetKinematicState();
    result.position = state.position;
    result.velocity = Vector3(0);
    result.movementFacing = state.movementFacing;
    result.torsoFacing = state.torsoFacing;
    player->ApplyKinematicResult(result);
    ProjectPlayerStateToSpatialState(player->GetKinematicState());
    player->CheckSimulationKinematicOracle();
  } else {
    result.position = spatialState.position;
    result.velocity = Vector3(0);
    result.movementFacing = spatialState.directionVec;
    result.torsoFacing = spatialState.bodyDirectionVec;
    ProjectPlayerStateToSpatialState(PlayerKinematicState{
        result.position, result.velocity, result.movementFacing, result.torsoFacing});
  }
}

void HumanoidBase::ProjectPlayerStateToSpatialState(
    const PlayerKinematicState &state) {
  DO_VALIDATION;
  spatialState.position = state.position;
  spatialState.movement = state.velocity;
  if (UsesProceduralLocomotion()) {
    // These serialized legacy aliases have no pure-locomotion producer.
    spatialState.actualMovement = state.velocity;
    spatialState.physicsMovement = state.velocity;
    spatialState.animMovement = state.velocity;
  }
  spatialState.directionVec = state.movementFacing;
  spatialState.floatVelocity = spatialState.movement.GetLength();
  spatialState.enumVelocity = FloatToEnumVelocity(spatialState.floatVelocity);
  spatialState.angle =
      ModulateIntoRange(-pi, pi, FixAngle(spatialState.directionVec.GetAngle2D()));
  ApplySimulationBodyState(state);
  // The next legacy evaluation derives raw movement from the position actually
  // in force. Collision corrections still update this compatibility input.
  previousPosition2D = spatialState.position;
}

void HumanoidBase::ApplySimulationBodyState(
    const PlayerKinematicState &state) {
  DO_VALIDATION;
  // Continuous simulation truth. Nothing in this function writes back into
  // state.torsoFacing, so legacy quantization cannot corrupt torso authority.
  spatialState.bodyDirectionVec = state.torsoFacing;
  spatialState.bodyAngle =
      spatialState.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  const Vector3 relative = state.torsoFacing.GetRotated2D(-spatialState.angle)
                               .GetNormalized(Vector3(0, -1, 0));
  spatialState.relBodyDirectionVecNonquantized = relative;
  spatialState.relBodyAngleNonquantized =
      relative.GetAngle2D(Vector3(0, -1, 0));

  // Compatibility for animation selection only. This quantization is a leaf:
  // bodyDirectionVec remains the continuous simulation projection above.
  spatialState.relBodyDirectionVec =
      ForceIntoAllowedBodyDirectionVec(relative);
  spatialState.relBodyAngle =
      spatialState.relBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
}


void HumanoidBase::ProjectMovementState(
    const PlayerKinematicState &tickStartState) {
  DO_VALIDATION;
  if (!player) return;
  if (!UsesProceduralLocomotion()) {
    // CalculateSpatialState already evaluated legacy animation kinematics,
    // applied them to PlayerState, and projected the compatibility state.
    return;
  }

  // Pure locomotion: the simulation produces the movement and the legacy
  // Humanoid movement fields follow it. The command and the starting state are
  // the tick-start ones, so an action selection later in this tick cannot
  // change what this tick's locomotion was. Writing spatialState.position here
  // means any later action already starts from the authoritative position
  // instead of an animation one.
  // The adapter consumes only the tick-start simulation state. Locomotion
  // produces lower-body motion first; the independent torso model then uses the
  // resulting facing and the command's look target.
  // The tick that consumes the command must see an exact copy of it.
  // Pure locomotion: one command source, the Player Decision Clock. Both the
  // movement model and the torso model consume the same decision-owned intent,
  // so the animation command no longer decides what executes.
  player->CheckDecisionLocomotionIntentOracle();
  const PlayerCommand &command = player->GetDecisionLocomotionIntent();
  // H3e4f-b: locomotion reads the simulation-owned copy, not the legacy anim.
  const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
      command, tickStartState,
      player->GetMaxVelocity(), tickStartState.torsoFacing);
  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = player->GetMaxVelocity();
  PlayerKinematicState next = tickStartState;
  PlayerLocomotion::Step(next, input, parameters, 0.01f);
  PlayerBodyFacingInput bodyInput;
  bodyInput.desiredFacing = next.movementFacing;
  if (command.useDesiredLookAt) {
    bodyInput.desiredFacing =
        (command.desiredLookAt - next.position).Get2D().GetNormalized(next.movementFacing);
  }
  PlayerBodyFacingParameters bodyParameters;
  PlayerBodyFacing::Step(next, bodyInput, bodyParameters, 0.01f);
  PlayerKinematicResult result;
  result.position = next.position;
  result.velocity = next.velocity;
  result.movementFacing = next.movementFacing;
  result.torsoFacing = next.torsoFacing;
  player->ApplyKinematicResult(result);
  ProjectPlayerStateToSpatialState(player->GetKinematicState());
  player->CheckSimulationKinematicOracle();
}

void HumanoidBase::AddTripCommandToQueue(PlayerCommandQueue &commandQueue,
                                         const Vector3 &tripVector,
                                         int tripType) {
  DO_VALIDATION;
  if (tripType == 1) {
    DO_VALIDATION;
    commandQueue.push_back(GetTripCommand(tripDirection, tripType));
  } else {
    // allow both types 2 and 3, but prefer the right one
    int otherTripType = 3;
    if (tripType == 3) otherTripType = 2;
    commandQueue.push_back(GetTripCommand(tripDirection, tripType));
    commandQueue.push_back(GetTripCommand(tripDirection, otherTripType));
    commandQueue.push_back(GetTripCommand(tripDirection, 1));
  }
}

PlayerCommand HumanoidBase::GetTripCommand(const Vector3 &tripVector,
                                           int tripType) {
  DO_VALIDATION;
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Trip;
  command.useDesiredMovement = false;
  command.useDesiredTripDirection = true;
  command.desiredTripDirection = tripVector;
  command.desiredVelocityFloat = spatialState.floatVelocity;//e_Velocity_Sprint;
  command.useTripType = true;
  command.tripType = tripType;
  return command;
}

PlayerCommand HumanoidBase::GetBasicMovementCommand(
    const Vector3 &desiredDirection, float velocityFloat) {
  DO_VALIDATION;
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  command.useDesiredLookAt = true;
  command.desiredDirection = spatialState.directionVec;
  command.desiredVelocityFloat = velocityFloat;
  command.desiredLookAt = spatialState.position + command.desiredDirection * 10.0f;
  return command;
}

void HumanoidBase::SetFootSimilarityPredicate(e_Foot desiredFoot) const {
  predicate_DesiredFoot = desiredFoot;
}

bool HumanoidBase::CompareFootSimilarity(e_Foot foot, int animIndex1, int animIndex2) const {
  int one = 1;
  int two = 1;
  if (anims->GetAnim(animIndex1)->GetCurrentFoot() == predicate_DesiredFoot) one = 0;
  if (anims->GetAnim(animIndex2)->GetCurrentFoot() == predicate_DesiredFoot) two = 0;
  if (FloatToEnumVelocity(anims->GetAnim(animIndex1)->GetIncomingVelocity()) == e_Velocity_Idle) one = 0;
  if (FloatToEnumVelocity(anims->GetAnim(animIndex2)->GetIncomingVelocity()) == e_Velocity_Idle) two = 0;
  return one < two;
}

void HumanoidBase::SetIncomingVelocitySimilarityPredicate(e_Velocity velocity) const {
  predicate_IncomingVelocity = velocity;
}

bool HumanoidBase::CompareIncomingVelocitySimilarity(int animIndex1, int animIndex2) const {
  /* old version
  float rating1 = std::fabs(clamp(RangeVelocity(anims->GetAnim(animIndex1)->GetIncomingVelocity()) - EnumToFloatVelocity(predicate_IncomingVelocity), -sprintVelocity, sprintVelocity));
  float rating2 = std::fabs(clamp(RangeVelocity(anims->GetAnim(animIndex2)->GetIncomingVelocity()) - EnumToFloatVelocity(predicate_IncomingVelocity), -sprintVelocity, sprintVelocity));
  */

  int currentVelocityID = GetVelocityID(predicate_IncomingVelocity);

  // rate difference anim incoming / actual incoming
  int anim1_incomingVelocityID = GetVelocityID(FloatToEnumVelocity(anims->GetAnim(animIndex1)->GetIncomingVelocity()));
  int anim2_incomingVelocityID = GetVelocityID(FloatToEnumVelocity(anims->GetAnim(animIndex2)->GetIncomingVelocity()));
  float rating1 = std::fabs(clamp(anim1_incomingVelocityID - currentVelocityID, -3, 3));
  float rating2 = std::fabs(clamp(anim2_incomingVelocityID - currentVelocityID, -3, 3));

  // also add a penalty for anim incoming velocities which aren't between actual incoming and anim outgoing
  int anim1_outgoingVelocityID = GetVelocityID(FloatToEnumVelocity(anims->GetAnim(animIndex1)->GetOutgoingVelocity()));
  int anim2_outgoingVelocityID = GetVelocityID(FloatToEnumVelocity(anims->GetAnim(animIndex2)->GetOutgoingVelocity()));
  if (anim1_incomingVelocityID > std::max(currentVelocityID, anim1_outgoingVelocityID)) rating1 += 0.5f;
  if (anim1_incomingVelocityID < std::min(currentVelocityID, anim1_outgoingVelocityID)) rating1 += 0.5f;
  if (anim2_incomingVelocityID > std::max(currentVelocityID, anim2_outgoingVelocityID)) rating2 += 0.5f;
  if (anim2_incomingVelocityID < std::min(currentVelocityID, anim2_outgoingVelocityID)) rating2 += 0.5f;

  return rating1 < rating2;
}

void HumanoidBase::SetMovementSimilarityPredicate(const Vector3 &relDesiredDirection, e_Velocity desiredVelocity) const {
  predicate_RelDesiredDirection = relDesiredDirection;
  predicate_DesiredVelocity = desiredVelocity;
  //if (relDesiredDirection.GetDotProduct(Vector3(0, -1, 0)) < 0) predicate_DesiredVelocity = e_Velocity_Idle;
  // this isn't working all too well: if targetmovement is set to 0 (aka corneringbias towards 1), both dribble @ 0 deg and dribble @ 90 deg will be the same distance (from 0), so still no preference for braking straight
  predicate_CorneringBias = CalculateBiasForFastCornering(Vector3(0, -1.0f * spatialState.floatVelocity, 0), relDesiredDirection * EnumToFloatVelocity(desiredVelocity), 1.0f, 0.9f); // anim space values!
}

float HumanoidBase::GetMovementSimilarity(int animIndex, const Vector3 &relDesiredDirection, e_Velocity desiredVelocity, float corneringBias) const {

  Vector3 desiredMovement = relDesiredDirection * EnumToFloatVelocity(desiredVelocity);

  Vector3 outgoingDirection = ForceIntoPreferredDirectionVec(anims->GetAnim(animIndex)->GetOutgoingDirection());
  float outgoingVelocity = RangeVelocity(anims->GetAnim(animIndex)->GetOutgoingVelocity());
  Vector3 outgoingMovement = outgoingDirection * outgoingVelocity;

  // anims that end at lower velocities have an advantage: they don't get dragged into the currentmovement that much
  // thus: have a bias that is higher at higher outgoing velocities, which means anim outgoingmovement gets more % of current movement and less % of their own
  // disabled for now - the new physics system disregards most of the anims movement anyway :)
  // *enabled again: altered the physics system so that it will regard anim movement more, so this became useful again for proper hard cornering

  // alternative to this system (drags back movement)
  //outgoingMovement += -desiredMovement.GetNormalized(0) * outgoingMovement.GetLength() * 0.5f;
  //desiredMovement = desiredMovement.GetNormalized(0) * clamp(desiredMovement.GetLength() - 0.8f, idleVelocity, sprintVelocity);

  desiredMovement = desiredMovement * (1.0f - corneringBias);

  float value = (desiredMovement - outgoingMovement).GetLength();

  value -= std::fabs(relDesiredDirection.GetDotProduct(outgoingDirection)) * 4.0f; // prefer straight lines (towards/away from desired outgoing)

/* disabled quantization:
  // maximum quantization (20 degree angle @ dribble velocity, smallest distance to be measured)
  // faulty calculation (cornering distance instead of straight line) for optimization, makes little difference anyway for 20 degrees
  float maxQuant = (dribbleVelocity * 2.0f * pi) / (360.0f / 20.0f);// * (1.0 - velocityBias * 0.5) // == ~1.22 at the moment of writing
  //float maxQuant = std::sqrt(2.0f * std::pow(dribbleVelocity, 2.0f) * (1.0f - std::cos(pi / 180.0f * 20.0f))); // the actual correct calculation (straight line)

  // add safety
  maxQuant *= 0.8f;

  // quantize
  value /= maxQuant;
  value = std::round(value);
  value *= maxQuant;
*/

  // never turn the wrong way around ** BUGGY, causes weird acceleration when we want idle velo **
  //if (std::fabs(outgoingDirection.GetAngle2D() - relDesiredDirection.GetAngle2D()) > pi) value += 100000;

  return value;
}

bool HumanoidBase::CompareMovementSimilarity(int animIndex1, int animIndex2) const {
  float rating1 = GetMovementSimilarity(animIndex1, predicate_RelDesiredDirection, predicate_DesiredVelocity, predicate_CorneringBias);
  float rating2 = GetMovementSimilarity(animIndex2, predicate_RelDesiredDirection, predicate_DesiredVelocity, predicate_CorneringBias);
  return rating1 < rating2;
}

bool HumanoidBase::CompareByOrderFloat(int animIndex1, int animIndex2) const {
  float rating1 = anims->GetAnim(animIndex1)->order_float;
  float rating2 = anims->GetAnim(animIndex2)->order_float;
  return rating1 < rating2;
}

void HumanoidBase::SetIncomingBodyDirectionSimilarityPredicate(const Vector3 &relIncomingBodyDirection) const {
  predicate_RelIncomingBodyDirection = relIncomingBodyDirection;
}

bool HumanoidBase::CompareIncomingBodyDirectionSimilarity(int animIndex1, int animIndex2) const {
  float rating1 = std::fabs(ForceIntoAllowedBodyDirectionVec(anims->GetAnim(animIndex1)->GetIncomingBodyDirection()).GetAngle2D(ForceIntoAllowedBodyDirectionVec(predicate_RelIncomingBodyDirection))) / pi;
  float rating2 = std::fabs(ForceIntoAllowedBodyDirectionVec(anims->GetAnim(animIndex2)->GetIncomingBodyDirection()).GetAngle2D(ForceIntoAllowedBodyDirectionVec(predicate_RelIncomingBodyDirection))) / pi;
  if (FloatToEnumVelocity(anims->GetAnim(animIndex1)->GetIncomingVelocity()) == e_Velocity_Idle) rating1 = 0;//-1;
  if (FloatToEnumVelocity(anims->GetAnim(animIndex2)->GetIncomingVelocity()) == e_Velocity_Idle) rating2 = 0;//-1;

  return rating1 < rating2;
}

void HumanoidBase::SetBodyDirectionSimilarityPredicate(const Vector3 &lookAt) const {
  predicate_LookAt = lookAt;
}

real HumanoidBase::DirectionSimilarityRating(int animIndex) const {
  Animation *a1 = anims->GetAnim(animIndex);
  Vector3 relDesiredBodyDirection1 = ((predicate_LookAt - spatialState.position).GetRotated2D(-spatialState.angle) - a1->GetTranslation()).GetNormalized(Vector3(0, -1, 0));
  radian maxAngleSmuggle = 0.1f * pi;
  radian outgoingAngle1 = a1->GetOutgoingDirection().GetRotated2D( clamp(predicate_RelDesiredDirection.GetAngle2D(a1->GetOutgoingDirection()), -maxAngleSmuggle, maxAngleSmuggle) ).GetAngle2D(Vector3(0, -1, 0));
  Vector3 predictedOutgoingBodyDirection1 = a1->GetOutgoingBodyDirection().GetRotated2D(outgoingAngle1);
  radian rating1 = std::fabs(predictedOutgoingBodyDirection1.GetAngle2D(relDesiredBodyDirection1));
  // penalty for body angles (as opposed to straight forward), to get a slight preference for forward angles
  rating1 += std::fabs(a1->GetOutgoingBodyAngle()) * 0.05f;
  return rating1;
}

bool HumanoidBase::CompareBodyDirectionSimilarity(int animIndex1, int animIndex2) const {
  return DirectionSimilarityRating(animIndex1) < DirectionSimilarityRating(animIndex2);
}

void HumanoidBase::SetTripDirectionSimilarityPredicate(const Vector3 &relDesiredTripDirection) const {
  predicate_RelDesiredTripDirection = relDesiredTripDirection;
}

bool HumanoidBase::CompareTripDirectionSimilarity(int animIndex1, int animIndex2) const {
  float rating1 = -GetVectorFromString(anims->GetAnim(animIndex1)->GetVariable("bumpdirection")).GetDotProduct(predicate_RelDesiredTripDirection);
  float rating2 = -GetVectorFromString(anims->GetAnim(animIndex2)->GetVariable("bumpdirection")).GetDotProduct(predicate_RelDesiredTripDirection);
  return rating1 < rating2;
}

bool HumanoidBase::CompareBaseanimSimilarity(int animIndex1, int animIndex2) const {
  bool isBase1 = anims->GetAnim(animIndex1)->GetVariableCache().baseanim();
  bool isBase2 = anims->GetAnim(animIndex2)->GetVariableCache().baseanim();

  if (isBase1 == true && isBase2 == false) return true;
  return false;
}

bool HumanoidBase::CompareCatchOrDeflect(int animIndex1, int animIndex2) const {
  bool catch1 = (anims->GetAnim(animIndex1)->GetVariable("outgoing_retain_state").compare("") != 0);
  bool catch2 = (anims->GetAnim(animIndex2)->GetVariable("outgoing_retain_state").compare("") != 0);

  if (catch1 == true && catch2 == false) return true;
  return false;
}

void HumanoidBase::SetIdlePredicate(float desiredValue) const {
  predicate_idle = desiredValue;
}

bool HumanoidBase::CompareIdleVariable(int animIndex1, int animIndex2) const {
  return std::fabs(anims->GetAnim(animIndex1)->GetVariableCache().idlelevel() - predicate_idle) <
         std::fabs(anims->GetAnim(animIndex2)->GetVariableCache().idlelevel() - predicate_idle);
}

bool HumanoidBase::ComparePriorityVariable(int animIndex1, int animIndex2) const {
  return std::fabs(atof(anims->GetAnim(animIndex1)->GetVariable("priority").c_str())) <
         std::fabs(atof(anims->GetAnim(animIndex2)->GetVariable("priority").c_str()));
}

Vector3 HumanoidBase::CalculatePhysicsVector(Animation *anim, bool useDesiredMovement, const Vector3 &desiredMovement, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, radian &rotationOffset_ret) const {

  positions_ret.clear();

  int animTouchFrame = atoi(anim->GetVariable("touchframe").c_str());
  bool touch = (animTouchFrame > 0);

  float stat_agility = player->GetStat(physical_agility);
  float stat_acceleration = player->GetStat(physical_acceleration);
  float stat_velocity = player->GetStat(physical_velocity);
  float stat_dribble = player->GetStat(technical_dribble);

  float incomingSwitchBias = 0.0f; // anything other than 0.0 may result in unpuristic behavior
  float outgoingSwitchBias = 0.0f;

  e_DefString animType = anim->GetAnimType();

  if (animType == e_DefString_BallControl) {
    DO_VALIDATION;
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Trap) {
    DO_VALIDATION;
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Interfere) {
    DO_VALIDATION;
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Deflect) {
    DO_VALIDATION;
    outgoingSwitchBias = 1.0f;
  } else if (animType == e_DefString_Sliding) {
    DO_VALIDATION;
    outgoingSwitchBias = 0.0f;
  } else if (animType == e_DefString_Special) {
    DO_VALIDATION;
    outgoingSwitchBias = 1.0f;
  } else if (animType == e_DefString_Trip) {
    DO_VALIDATION;
    outgoingSwitchBias = 0.5f; // direction partly predecided by collision function in match class
  } else if (touch) {
    DO_VALIDATION;
    outgoingSwitchBias = 1.0f;
  }

  if (anim->GetVariableCache().incoming_special_state().compare("") != 0 ||
      anim->GetVariableCache().outgoing_special_state().compare("") != 0) outgoingSwitchBias = 1.0f;

  Vector3 animIncomingMovement = Vector3(0, -1, 0).GetRotated2D(spatialState.angle) * RangeVelocity(anim->GetIncomingVelocity());
  Vector3 adaptedCurrentMovement = animIncomingMovement * incomingSwitchBias + spatialState.movement * (1.0f - incomingSwitchBias);

  Vector3 predictedOutgoingMovement = anim->GetOutgoingMovement().GetRotated2D(spatialState.angle);
  Vector3 velocifiedDesiredMovement = (useDesiredMovement) ? desiredMovement : predictedOutgoingMovement;

  assert(desiredMovement.coords[2] == 0.0f);

  Vector3 adaptedDesiredMovement = predictedOutgoingMovement * outgoingSwitchBias + velocifiedDesiredMovement * (1.0f - outgoingSwitchBias);
  assert(predictedOutgoingMovement.coords[2] == 0.0f);
  assert(velocifiedDesiredMovement.coords[2] == 0.0f);
  assert(adaptedDesiredMovement.coords[2] == 0.0f);

  float maxVelocity = player->GetMaxVelocity();
  if (touch) maxVelocity *= 0.92f;

  Vector3 resultingMovement;

  const int timeStep_ms = 10;

  bool isBaseAnim = anim->GetVariableCache().baseanim();

  float difficultyFactor = atof(anim->GetVariable("animdifficultyfactor").c_str());
  float difficultyPenaltyFactor = std::pow(
      clamp((difficultyFactor - 0.0f) *
                (1.0f - (stat_agility * 0.2f + stat_acceleration * 0.2f)) *
                2.0f,
            0.0f, 1.0f),
      0.7f);

  float powerFactor = 1.0f - clamp(std::pow(player->GetLastTouchBias(1000), 0.8f) * (0.8f - stat_dribble * 0.3f), 0.0f, 0.4f);
  powerFactor *= 1.0f - clamp(decayingPositionOffset.GetLength() * (10.0f - player->GetStat(physical_balance) * 5.0f) - 0.1f, 0.0f, 0.3f);

  Vector3 temporalMovement = adaptedCurrentMovement;

  assert(adaptedCurrentMovement.coords[2] == 0.0f);
  assert(adaptedDesiredMovement.coords[2] == 0.0f);

  // orig anim positions

  const std::vector<Vector3> &origPositionCache = match->GetAnimPositionCache(anim);

  Vector3 currentPosition;

  // amount of pure physics that 'shines through' pure anim
  float physicsBias = 1.0f;
  // angle deviation away from anim
  radian maxAngleMod_underAnimAngle = 0.125f * pi;
  radian maxAngleMod_overAnimAngle = 0.125f * pi;
  radian maxAngleMod_straightAnimAngle = 0.125f * pi;
  if (touch) {
    DO_VALIDATION;
    float bonus = 1.0f - std::pow(NormalizedClamp((adaptedCurrentMovement +
                                                   predictedOutgoingMovement)
                                                          .GetLength() *
                                                      0.5f,
                                                  0, sprintVelocity),
                                  0.8f) *
                             0.8f;
    bonus *= 0.6f + 0.4f * player->GetStat(technical_ballcontrol);
    maxAngleMod_underAnimAngle = 0.2f * pi * bonus;
    maxAngleMod_overAnimAngle = 0;
    maxAngleMod_straightAnimAngle = 0.1f * pi * bonus;
  }
  if (animType == e_DefString_Sliding) {
    DO_VALIDATION;
    maxAngleMod_underAnimAngle = 0.5f * pi;
    maxAngleMod_overAnimAngle = 0.5f * pi;
    maxAngleMod_straightAnimAngle = 0.5f * pi;
  }

  if (animType == e_DefString_Movement)    { physicsBias *= 1.0f; }

  if (animType == e_DefString_BallControl) {
    DO_VALIDATION;
    physicsBias *= 1.0f;
  }
  if (animType== e_DefString_Trap)        { physicsBias *= 1.0f; }

  if (animType== e_DefString_ShortPass)   { physicsBias *= 0.0f; }
  if (animType== e_DefString_HighPass)    { physicsBias *= 0.0f; }
  if (animType== e_DefString_Shot)        { physicsBias *= 0.0f; }

  if (animType== e_DefString_Interfere)   { physicsBias *= 0.5f; }
  if (animType== e_DefString_Deflect)     { physicsBias *= 0.0f; }

  if (animType== e_DefString_Sliding)     { physicsBias *= 1.0f; }
  if (animType== e_DefString_Trip)        { if (anim->GetVariable("triptype").compare("1") == 0) physicsBias *= 0.5f; else physicsBias *= 0.0f; }

  if (animType== e_DefString_Special)     { physicsBias *= 0.0f; }
  if (anim->GetVariableCache().incoming_special_state().compare("") != 0)
                                            { physicsBias *= 0.0f; }

  bool mod_AllowRotation = true;
  bool mod_CorneringBraking = false;
  bool mod_PointinessCurve = true;
  bool mod_MaximumAccelDecel = false;
  bool mod_BrakeOnTouch = false; // may be too pointy for anims near 90 degree
  bool mod_MaxCornering = true;
  bool mod_MaxChange = true;
  bool mod_AirResistance = true;
  bool mod_CheatBodyDirection = false;

  float accelerationMultiplier = 0.5f + _default_AccelerationFactor;

  // rotate anim towards desired angle

  radian toDesiredAngle_capped = 0;
  if (mod_AllowRotation && physicsBias > 0.0f) {
    DO_VALIDATION;
    Vector3 animOutgoingVector = predictedOutgoingMovement.GetNormalized(0);
    if (FloatToEnumVelocity(predictedOutgoingMovement.GetLength()) == e_Velocity_Idle) animOutgoingVector = anim->GetOutgoingDirection().GetRotated2D(spatialState.angle);
    Vector3 desiredVector = adaptedDesiredMovement.GetNormalized(0);
    if (FloatToEnumVelocity(adaptedDesiredMovement.GetLength()) == e_Velocity_Idle) desiredVector = desiredBodyDirectionRel.GetRotated2D(spatialState.angle);
    radian toDesiredAngle = desiredVector.GetAngle2D(animOutgoingVector);
    if (std::fabs(toDesiredAngle) <= 0.5f * pi || animType == e_DefString_Sliding) {
      DO_VALIDATION;  // if we want > x degrees, just skip it to next anim,
                      // it'll only look weird otherwise

      radian animChange = animOutgoingVector.GetAngle2D(spatialState.directionVec);
      if (std::fabs(animChange) > 0.06f * pi) {
        DO_VALIDATION;
        int sign = signSide(animChange);
        if (signSide(toDesiredAngle) == sign) {
          DO_VALIDATION;
          toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_overAnimAngle, maxAngleMod_overAnimAngle);
        } else {
          toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_underAnimAngle, maxAngleMod_underAnimAngle);
        }
      } else {
        // straight ahead anim, has no specific side.
        toDesiredAngle_capped = clamp(toDesiredAngle, -maxAngleMod_straightAnimAngle, maxAngleMod_straightAnimAngle);
      }
      //printf("animChange: %f radians, to desired angle: %f radians, sign: %i, sign side of to desired angle: %i, resulting capped angle: %f radians\n", animChange, toDesiredAngle, sign, signSide(toDesiredAngle), toDesiredAngle_capped);
    }
  }

  float maximumOutgoingVelocity = sprintVelocity;
  // brake on cornering
  if (mod_CorneringBraking) {
    DO_VALIDATION;

    float brakeBias = 0.8f;
    brakeBias *= (touch) ? 1.0f : 0.8f;
    brakeBias *= (1.0f - stat_agility * 0.2f);

    Vector3 animOutgoingMovement = anim->GetOutgoingMovement();
    animOutgoingMovement.Rotate2D(toDesiredAngle_capped);
    brakeBias *= std::pow(NormalizedClamp(spatialState.floatVelocity,
                                          idleVelocity, sprintVelocity - 0.5f),
                          0.8f);  // 0.5f);
    float maxVelo =
        sprintVelocity *
        ((1.0f - brakeBias) +
         ((1.0f -
           std::pow(std::fabs(animOutgoingMovement.GetNormalized(0).GetAngle2D(
                             Vector3(0, -1, 0)) /
                         pi),
                    0.5f)) *
          brakeBias));
    maximumOutgoingVelocity = maxVelo;
  }

  // --- loop da loop ------------------------------------------------------------------------------------------------------------------------------------------
  for (int time_ms = 0; time_ms < anim->GetFrameCount() * 10;
       time_ms += timeStep_ms) {
    DO_VALIDATION;

    // start with +1, because we want to influence the first frame as well
    // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
    // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
    float frameBias = (time_ms + 10) / (float)((anim->GetEffectiveFrameCount() + 1) * 10);

    float lagExp = 1.0f;
    if (mod_PointinessCurve && physicsBias > 0.0f &&
        (animType == e_DefString_BallControl ||
         animType == e_DefString_Movement)) {
      DO_VALIDATION;
      lagExp = 1.4f - _default_AgilityFactor * 0.8f;
      lagExp *= 1.2f - stat_agility * 0.4f;
      if (touch) {
        DO_VALIDATION;
        lagExp += -0.1f + clamp(difficultyFactor * 0.4f, 0.0f, 0.5f);
      } else {
        lagExp += -0.2f + clamp(difficultyFactor * 0.2f, 0.0f, 0.2f);
      }

      lagExp = clamp(lagExp, 0.25f, 4.0f);
      if (touch && time_ms < animTouchFrame * 10) lagExp = std::max(lagExp, 0.7f); // else, we could 'miss' the ball because we're already turned around too much

      lagExp = lagExp * physicsBias + 1.0f * (1.0f - physicsBias);
    }
    float adaptedFrameBias = std::pow(frameBias, lagExp);
    Vector3 animMovement = CalculateMovementAtFrame(origPositionCache, anim->GetEffectiveFrameCount() * adaptedFrameBias, 1).GetRotated2D(spatialState.angle);

    float animVelo = animMovement.GetLength();
    Vector3 adaptedAnimMovement = animMovement;
    float adaptedAnimVelo = animVelo;

    // adapt sprint velocity to player's max velocity stat

    if (animVelo > walkSprintSwitch &&
        (animType == e_DefString_Movement ||
         animType == e_DefString_BallControl || animType == e_DefString_Trap)) {
      DO_VALIDATION;

      if (maxVelocity > animVelo) {
        DO_VALIDATION;  // only speed up, don't slow down. may be faster parts
                        // (jumps and such) within anim, allow this
        adaptedAnimVelo = StretchSprintTo(animVelo, animSprintVelocity, maxVelocity);
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    float maxSlower = 1.6f; // rationale: don't want to end up below dribbleVelocity - idleDribbleSwitch (= change velocity)
    if (touch) maxSlower = 1.2f;
    float maxFaster = 0.0f;
    if (touch) maxFaster = 0.0f;
    if (temporalMovement.GetLength() > adaptedAnimVelo) maxFaster = std::min(0.0f + 1.0f * (1.0f - frameBias), std::max(maxFaster, temporalMovement.GetLength() - adaptedAnimVelo)); // ..already going faster.. well okay, allow this
    if (maxFaster > 0) maxFaster *= std::max(0.0f, adaptedAnimMovement.GetNormalizedMax(1.0f).GetDotProduct(adaptedDesiredMovement.GetNormalized(0))); // only go faster if it's in the right direction
    if (animType== e_DefString_Sliding) maxFaster = 100;
    float desiredVelocity = adaptedDesiredMovement.GetLength();
    adaptedAnimVelo = clamp(desiredVelocity, adaptedAnimVelo - maxSlower, adaptedAnimVelo + maxFaster);
    adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;

    if (mod_CorneringBraking) {
      DO_VALIDATION;
      float frameBiasedMaximumOutgoingVelocity = sprintVelocity * (1.0f - frameBias) + maximumOutgoingVelocity * frameBias;
      if (adaptedAnimVelo > frameBiasedMaximumOutgoingVelocity) {
        DO_VALIDATION;
        adaptedAnimVelo = frameBiasedMaximumOutgoingVelocity;
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    if (mod_MaximumAccelDecel) {
      DO_VALIDATION;
      // this is basically meant to enforce transitions to be smoother, disallowing bizarre steps. however, with low enough max values, it can also serve as a physics slowness thing.
      // in that regard, the maxaccel part is somewhat similar to the air resistance mod below. they can live together; this one can serve as a constant maximum, and the air resistance as a velocity-based maximum.
      // update: decided to not let them live together, this should now purely be used for capping transition speed
      float maxAccelMPS = 20.0f;
      float maxDecelMPS = 20.0f;
      float currentVelo = temporalMovement.GetLength();
      float veloChangeMPS = (adaptedAnimVelo - currentVelo) / ((float)timeStep_ms * 0.001f);
      if (veloChangeMPS < -maxDecelMPS || veloChangeMPS > maxAccelMPS) {
        DO_VALIDATION;
        adaptedAnimVelo = currentVelo + clamp(veloChangeMPS, -maxDecelMPS, maxAccelMPS) * ((float)timeStep_ms * 0.001f);
        adaptedAnimMovement = adaptedAnimMovement.GetNormalized(0) * adaptedAnimVelo;
      }
    }

    Vector3 resultingPhysicsMovement = adaptedAnimMovement;

    // angle
    resultingPhysicsMovement = resultingPhysicsMovement.GetRotated2D(toDesiredAngle_capped * frameBias);// * std::pow(frameBias, 0.6f));

    // --- stay true to anim? -----------------------------------------------------------------------------------------------------------------------

    resultingPhysicsMovement = resultingPhysicsMovement * physicsBias + animMovement * (1.0f - physicsBias);

    // that's it, we now know where we want to go in life

    Vector3 toDesired = (resultingPhysicsMovement - temporalMovement);

    // --- end --------------------------------------------------------------------------------------------------------------------------------------

    assert(toDesired.coords[2] == 0.0f);

    float penaltyBreakFactor = 0.0f;
    if (mod_BrakeOnTouch) {
      DO_VALIDATION;
      // slow down after touching ball
      // (precalc at touchframe, because temporalMovement will change because of this, so if we don't precalc then changing numBrakeFrames will change the amount of effect)
      int numBrakeFrames = 15;
      if (touch && time_ms >= animTouchFrame * 10 &&
          time_ms < (animTouchFrame + numBrakeFrames) * 10) {
        DO_VALIDATION;
        int brakeFramesInto = (time_ms - (animTouchFrame * 10)) / 10;
        float brakeFrameFactor =
            std::pow(1.0f - (brakeFramesInto / (float)numBrakeFrames), 0.5f);

        float touchBrakeFactor = 0.3f;

        float touchDifficultyFactor = clamp((difficultyFactor + 0.7f) * (1.0f - stat_dribble * 0.4f), 0.0, 1.0f);
        touchDifficultyFactor *=
            1.0f -
            std::pow(
                std::fabs(anim->GetOutgoingAngle()) / pi,
                0.75f);  // don't help with braking (when going nearer 180 deg)

        float veloFactor = NormalizedClamp(temporalMovement.GetLength(), walkVelocity, sprintVelocity);

        penaltyBreakFactor = touchDifficultyFactor * veloFactor * brakeFrameFactor * touchBrakeFactor;
      }
    }

    /* part of new method, but needs some debugging/unittesting
    // increase over multiple frames for smoother effect, and so we get a proper
    effect even if maxChange isn't very high int numBrakeFrames = 5; if (touch
    && time_ms >= animTouchFrame * 10 && time_ms < (animTouchFrame +
    numBrakeFrames) * 10) { DO_VALIDATION; int framesInto = (time_ms -
    (animTouchFrame * 10)) / 10; toDesired += -temporalMovement.GetNormalized(0)
    * (ballTouchSlowdownAmount / (float)(numBrakeFrames - framesInto));
    }
    */

    if (mod_MaxCornering) {
      DO_VALIDATION;
      Vector3 predictedMovement = temporalMovement + toDesired;
      float startVelo = idleDribbleSwitch;
      if (temporalMovement.GetLength() > startVelo &&
          predictedMovement.GetLength() > startVelo) {
        DO_VALIDATION;
        radian angle = predictedMovement.GetNormalized().GetAngle2D(temporalMovement.GetNormalized());
        float maxAngleFactor = 1.0f * (timeStep_ms / 1000.0f);
        maxAngleFactor *= (0.7f + 0.3f * stat_agility);
        if (!touch) maxAngleFactor *= 1.5f;
        radian maxAngle = maxAngleFactor * pi;
        float veloFactor = std::pow(
            NormalizedClamp(temporalMovement.GetLength(), 0, sprintVelocity),
            1.0f);
        maxAngle /= (veloFactor + 0.01f);

        if (std::fabs(angle) > maxAngle) {
          DO_VALIDATION;

          int mode = 1; // 0: restrict max angle, 1: restrict velocity

          if (mode == 0) {
            DO_VALIDATION;
            Vector3 restrictedPredictedMovement = predictedMovement.GetRotated2D((std::fabs(angle) - maxAngle) * -signSide(angle));
            Vector3 newToDesired = restrictedPredictedMovement - temporalMovement;
            toDesired = newToDesired;
          } else if (mode == 1) {
            DO_VALIDATION;
            radian overAngle = std::fabs(angle) - maxAngle; // > 0
            toDesired += -temporalMovement * clamp(overAngle / pi * 3.0f, 0.0f, 1.0f);// was: 3
          }
        }
      }
    }

    if (mod_MaxChange) {
      DO_VALIDATION;
      float maxChange = 0.03f;
      if (animType== e_DefString_Trip) maxChange *= 0.7f;
      if (animType== e_DefString_Sliding) maxChange = 0.1f;
      // no power first few frames, so transitions are smoother
      //maxChange *= 0.3f + 0.7f * curve(NormalizedClamp(time_ms, 0.0f, 80.0f), 1.0f);
      float veloFactor = std::pow(
          NormalizedClamp(temporalMovement.GetLength(), 0, sprintVelocity),
          1.5f);
      float firstStepFactor = veloFactor;
      if (animType == e_DefString_Movement) firstStepFactor *= 0.4f;
      maxChange *= (1.0f - firstStepFactor) + firstStepFactor * curve(NormalizedClamp(time_ms, 0.0f, 160.0f), 1.0f);

      maxChange *= 1.2f - veloFactor * 0.4f;

      maxChange *= 0.75f + _default_AgilityFactor * 0.5f;

      // lose power 'around' touch

      // if (touch && time_ms >= animTouchFrame * 10) { DO_VALIDATION;
      //   int influenceFrames = 16; // number of frames to slow down on each
      //   'side' of the balltouch
      //   //float frameBias = NormalizedClamp(std::fabs((animTouchFrame * 10) -
      //   time_ms), 0, influenceFrames * 10) * 0.7f + 0.3f; float frameBias =
      //   NormalizedClamp(time_ms - (animTouchFrame * 10), 0, influenceFrames *
      //   10) * 1.0f;// + 0.1f; frameBias = curve(frameBias, 1.0f); maxChange
      //   *= clamp(frameBias, 0.1f, 1.0f);
      // }

      maxChange *= powerFactor;

      float desiredLength = toDesired.GetLength();
      float maxAddition = maxChange * timeStep_ms;

      toDesired.NormalizeMax(std::min(desiredLength, maxAddition));
    }

    // air resistance

    if (mod_AirResistance && animType != e_DefString_Sliding &&
        animType != e_DefString_Deflect) {
      DO_VALIDATION;
      float veloExp = 1.8f;
      float accelPower = 11.0f * accelerationMultiplier;
      float falloffStartVelo = idleDribbleSwitch;

      if ((temporalMovement + toDesired).GetLength() > falloffStartVelo) {
        DO_VALIDATION;

        // less accelpower on tough anims
        accelPower *= 1.0f - difficultyPenaltyFactor * 0.4f;

        // wolfram alpha to show difference in sin before/after exp order: 10 * (std::sin(((1.0 - x ^ 2.0) - 0.5) * pi) * 0.5 + 0.5), 10 * ((1.0 - (std::sin((x - 0.5) * pi) * 0.5 + 0.5)) ^ 2.0) | from x = 0.0 to 1.0

        float veloAirResistanceFactor = clamp(
            std::pow(clamp((temporalMovement.GetLength() - falloffStartVelo) /
                               (player->GetMaxVelocity() - falloffStartVelo),
                           0.0f, 1.0f),
                     veloExp),
            0.0f, 1.0f);

        // simulate footsteps - powered in the middle
        //veloAirResistanceFactor = 1.0f - ((1.0f - veloAirResistanceFactor) * std::pow(sin(frameBias * pi), 2.0f));

        // circular version
        Vector3 forwardVector;
        if ((temporalMovement + toDesired).GetLength() >
            temporalMovement.GetLength()) {
          DO_VALIDATION;  // outside the 'velocity circle'
          Vector3 destination = temporalMovement + toDesired;
          float velo = temporalMovement.GetLength();
          float accel = destination.GetLength() - velo;
          forwardVector = destination.GetNormalized(0) * accel;
        }

        float accelerationAddition = forwardVector.GetLength();
        float maxAccelerationMPS = accelPower * (1.0f - veloAirResistanceFactor) * (stat_acceleration * 0.3f + 0.7f);
        float maxAccelerationAddition = maxAccelerationMPS * (timeStep_ms / 1000.0f);
        if (accelerationAddition > maxAccelerationAddition) {
          DO_VALIDATION;
          float remainingFactor = maxAccelerationAddition / accelerationAddition;
          toDesired = toDesired - forwardVector * (1.0f - remainingFactor);
        }
      }
    }

    // MAKE IT SEW! http://static.wixstatic.com/media/fc58ad_c0ef2d69d98f4f7ba8e7e488f0e28ece.jpg

    Vector3 tmpTemporalMovement = temporalMovement + toDesired;

    // make sure outgoing velocity is of the same idleness as the anim
    if (time_ms >= (anim->GetFrameCount() - 2) * 10) {
      DO_VALIDATION;

      bool hardQuantize = true;
      if (!hardQuantize && anim->GetVariableCache().outgoing_special_state().compare("") != 0) hardQuantize = true;

      if (!hardQuantize) {
        DO_VALIDATION;
        // soft version
        if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) == e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) != e_Velocity_Idle) tmpTemporalMovement.NormalizeTo(idleDribbleSwitch - 0.01f);
        else if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) != e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) == e_Velocity_Idle) tmpTemporalMovement = anim->GetOutgoingMovement().GetRotated2D(spatialState.angle).GetNormalizedTo(idleDribbleSwitch + 0.01f);
      } else {
        // hard version
        if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) == e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) != e_Velocity_Idle) tmpTemporalMovement = 0;
        else if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) != e_Velocity_Idle && FloatToEnumVelocity(tmpTemporalMovement.GetLength()) == e_Velocity_Idle) tmpTemporalMovement = anim->GetOutgoingMovement().GetRotated2D(spatialState.angle).GetNormalizedTo(dribbleVelocity);
      }
    }

    assert(tmpTemporalMovement.coords[2] == 0.0f);
    temporalMovement = tmpTemporalMovement;

    if (time_ms >= (anim->GetFrameCount() - 2) * 10) penaltyBreakFactor = 0.0f;
    currentPosition += temporalMovement * (1.0f - penaltyBreakFactor) * (timeStep_ms / 1000.0f);
    assert(currentPosition.coords[2] == 0.0f);

    if (time_ms % 10 == 0) {
      DO_VALIDATION;
      positions_ret.push_back(currentPosition);
    }

    /*
    // dynamic timestep: more precision at high velocities
    if ((int)time_ms % 10 == 0) { DO_VALIDATION;
      //if (temporalMovement.GetLength() > walkVelocity) timeStep_ms = 5; else
    timeStep_ms = 10; timeStep_ms = 10;
    }
    */
  }

  assert(positions_ret.size() >= (unsigned int)anim->GetFrameCount());
  resultingMovement = temporalMovement;

  if (FloatToEnumVelocity(anim->GetOutgoingVelocity()) != e_Velocity_Idle &&
      FloatToEnumVelocity(resultingMovement.GetLength()) != e_Velocity_Idle) {
    DO_VALIDATION;
    rotationOffset_ret = resultingMovement.GetRotated2D(-spatialState.angle).GetAngle2D(anim->GetOutgoingMovement());
  } else {
    rotationOffset_ret = toDesiredAngle_capped * physicsBias;
  }

  // body direction assist
  if (mod_CheatBodyDirection && useDesiredBodyDirection &&
      animType == e_DefString_Movement) {
    DO_VALIDATION;

    float angleFactor = 0.5f;
    radian maxAngle = 0.25f * pi;

    radian predictedAngleRel = anim->GetOutgoingAngle() + anim->GetOutgoingBodyAngle() + rotationOffset_ret;
    radian desiredRotationOffset = desiredBodyDirectionRel.GetRotated2D(-predictedAngleRel).GetAngle2D(Vector3(0, -1, 0));

    if (std::fabs(desiredRotationOffset) < 0.5f * pi) {
      DO_VALIDATION;  // else: too much

      float outgoingVelocityFactorInv = 1.0f - NormalizedClamp(resultingMovement.GetLength(), idleDribbleSwitch, sprintVelocity - 1.0f) * 1.0f;
      float animLengthFactor = NormalizedClamp(anim->GetFrameCount(), 0, 25);
      radian maximizedRotationOffset = clamp(desiredRotationOffset, outgoingVelocityFactorInv * animLengthFactor * angleFactor * -maxAngle,
                                                                    outgoingVelocityFactorInv * animLengthFactor * angleFactor *  maxAngle);

      rotationOffset_ret += maximizedRotationOffset;
    }
  }

  assert(resultingMovement.coords[2] == 0.0f);

  return resultingMovement;
}

Vector3 HumanoidBase::ForceIntoAllowedBodyDirectionVec(const Vector3 &src) const {

  // check what allowed dir this vector is closest to
  float bestDot = -1.0f;
  Vector3 best;
  for (const Vector3 &vec : allowedBodyDirVecs) {
    DO_VALIDATION;
    float nDotL = vec.GetDotProduct(src);
    if (nDotL > bestDot) {
      DO_VALIDATION;
      bestDot = nDotL;
      best = vec;
    }
  }

  return best;
}

radian HumanoidBase::ForceIntoAllowedBodyDirectionAngle(radian angle) const {

  float bestAngleDiff = 10000.0;
  radian bestValue = 0;
  for (auto v : allowedBodyDirAngles) {
    DO_VALIDATION;
    float diff = std::fabs(v - angle);
    if (diff < bestAngleDiff) {
      DO_VALIDATION;
      bestAngleDiff = diff;
      bestValue = v;
    }
  }
  return bestValue;
}

Vector3 HumanoidBase::ForceIntoPreferredDirectionVec(const Vector3 &src) const {

  float bestDot = -1.0f;
  Vector3 bestValue;
  for (auto &v : preferredDirectionVecs) {
    DO_VALIDATION;
    float nDotL = v.GetDotProduct(src);
    if (nDotL > bestDot) {
      DO_VALIDATION;
      bestDot = nDotL;
      bestValue = v;
    }
  }
  return bestValue;
}

radian HumanoidBase::ForceIntoPreferredDirectionAngle(radian angle) const {

  float bestAngleDiff = 10000.0;
  radian bestValue;
  for (auto v : preferredDirectionAngles) {
    DO_VALIDATION;
    float diff = std::fabs(v - angle);
    if (diff < bestAngleDiff) {
      DO_VALIDATION;
      bestAngleDiff = diff;
      bestValue = v;
    }
  }
  return bestValue;
}

void HumanoidBase::ProcessState(EnvState *state) {
  DO_VALIDATION;
  currentAnim.ProcessState(state);
  state->process(previousAnim_frameNum);
  state->process(previousAnim_functionType);
  state->process(startPos);
  state->process(startAngle);
  state->process(nextStartPos);
  state->process(nextStartAngle);
  spatialState.ProcessState(state);
  state->process(previousPosition2D);
  state->process(interruptAnim);
  state->process(reQueueDelayFrames);
  state->process(tripType);
  state->process(tripDirection);
  state->process(decayingPositionOffset);
  state->process(decayingDifficultyFactor);
  state->process(mentalImageTime);
}

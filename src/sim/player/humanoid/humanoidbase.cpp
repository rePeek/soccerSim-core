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
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include "sim/player/humanoid/humanoid.hpp"

#include "sim/player/humanoid/humanoid_utils.hpp"

#include "sim/player/player.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/player/legacy_locomotion_command.hpp"
#include "sim/player/player_locomotion.hpp"
#include "sim/player/player_body_facing.hpp"

#include "sim/player/player_motion_constants.hpp"
#include "sim/player/player_motion.hpp"



#include "football/animation/selector.hpp"
#include "football/animation/library.hpp"

using football::ball::Ball;


using std::placeholders::_1;
using std::placeholders::_2;
constexpr float bodyRotationSmoothingFactor = 1.0f;
constexpr float bodyRotationSmoothingMaxAngle = 0.25f * pi;
constexpr float initialReQueueDelayFrames = 32;


int &HumanoidProceduralMovementTicks() { static int value = 0; return value; }
LocomotionPlaybackAudit &LocomotionPlaybackAuditState() { static LocomotionPlaybackAudit value; return value; }
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

// Field-for-field comparison of the fields a replayed SelectAnim would read.
// Floats and vectors compare bit-exact, so a new field has to be added here too.
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
                                     int frame_count) {
  if (command.desiredFunctionType != e_FunctionType_Movement) return;
  if (material_candidate) ++HumanoidMaterialCommandsAccepted();
  ++HumanoidMovementSelections();
  HumanoidSelectedMovementFrames().push_back(frame_count);
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
      throw std::logic_error(
          "ContactAuthorityFor: untracked scheduled contact type");
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

void RecordFootCounterfactual(const AnimationLibrary& animations, int with_foot_head,
                              int without_foot_head) {
  ++HumanoidFootCounterfactualSelections();
  if (with_foot_head == without_foot_head) return;
  ++HumanoidFootWinnerChanged();
  const AnimationClip &cw = animations.Get(static_cast<uint32_t>(with_foot_head));
  const AnimationClip &co = animations.Get(static_cast<uint32_t>(without_foot_head));
  bool lifecycle_changed = false;
  if (cw.frame_count != co.frame_count) {
    ++HumanoidFootFrameCountDiff();
    lifecycle_changed = true;
  }
  if (cw.metadata.quadrant !=
      co.metadata.quadrant) {
    ++HumanoidFootQuadrantDiff();
    lifecycle_changed = true;
  }
  if (FloatToEnumVelocity(cw.metadata.outgoing_velocity) !=
      FloatToEnumVelocity(co.metadata.outgoing_velocity)) {
    ++HumanoidFootOutgoingVelocityDiff();
    lifecycle_changed = true;
  }
  const float with_angle = cw.metadata.outgoing_angle;
  const float without_angle = co.metadata.outgoing_angle;
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
  if (cw.metadata.outgoing_special_state !=
          co.metadata.outgoing_special_state ||
      cw.metadata.incoming_special_state !=
          co.metadata.incoming_special_state) {
    ++HumanoidFootSpecialStateDiff();
    lifecycle_changed = true;
  }
  if (lifecycle_changed) ++HumanoidFootLifecycleChanged();
}


HumanoidBase::HumanoidBase(Player *player, const AnimationLibrary& animations, football::math::Rng& rng)
    : animations_(animations), rng_(rng),
      player(player) {
  interruptAnim = e_InterruptAnim_None;
  reQueueDelayFrames = 0;
  decayingPositionOffset = Vector3(0);
  decayingDifficultyFactor = 0.0f;

  ResetPosition(Vector3(0), Vector3(0));
}

HumanoidBase::~HumanoidBase() {}

const AnimationClip &HumanoidBase::GetBakedClip(AnimationId id) const {
  return animations_.Get(static_cast<uint32_t>(id));
}

football::sim::SelectedAction HumanoidBase::CurrentSelectedAction() const {
  football::sim::SelectedAction selection;
  selection.clip_id = currentAnim.animationId;
  selection.function_type = currentAnim.functionType;
  selection.start_frame = currentAnim.frameNum;
  selection.frame_count = static_cast<int>(GetBakedClip(currentAnim.animationId).frame_count);
  selection.touch_frame = currentAnim.touchFrame;
  selection.touch_position = currentAnim.touchPos;
  return selection;
}

const AnimationClip &HumanoidBase::GetCurrentBakedClip() const {
  return GetBakedClip(currentAnim.animationId);
}


float &HumanoidBase::OrderScratch(int id) const {
  if (orderScratch_.size() <= static_cast<size_t>(id)) {
    orderScratch_.resize(static_cast<size_t>(id) + 1, 0.0f);
  }
  return orderScratch_[static_cast<size_t>(id)];
}
void HumanoidBase::Mirror() {
  // Mirrors the field-level legacy contract, not a whole-state coordinate
  // transform: SpatialState::Mirror negates the movement fields but leaves the
  // facing/body fields untouched. Player::Mirror() mirrors its kinematic
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

void HumanoidBase::Process(football::sim::Tick now, const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::AcceptedTouchSink& /*touch_sink*/, football::sim::PlayerRuntimeSink& /*runtime_sink*/) {
  // Reject invalid runtime state before the spatial/action debug oracles run.
  if (startPos.coords[2] != 0.f) {
    throw std::logic_error("HumanoidBase::Process: player position must have zero height");
  }

  decayingPositionOffset *= 0.95f;
  if (decayingPositionOffset.GetLength() < 0.005) decayingPositionOffset.Set(0);
  decayingDifficultyFactor = clamp(decayingDifficultyFactor - 0.002f, 0.0f, 1.0f);



  // See Humanoid::Process: the tick-start movement state is authoritative.
  const PlayerKinematicState tickStartState = player->GetKinematicState();

  CalculateSpatialState(tick.ball_retainer);
  spatialState.positionOffsetMovement = Vector3(0);
  ProjectMovementState(tickStartState, tick.ball_retainer, tick.restart_needs_simulation);

  player->AdvanceActionTick();

  const PlayerActionState &action = player->GetSimulationActionState();
  previousAnim_frameNum++;

  if (action.IsAtLastFrame() && interruptAnim == e_InterruptAnim_None) {
    interruptAnim = e_InterruptAnim_Switch;
  }

  bool mayReQueue = false;

  // already some anim interrupt waiting?

  if (mayReQueue) {
    if (interruptAnim != e_InterruptAnim_None) {
      mayReQueue = false;
    }
  }

  // okay, see if we need to requeue

  if (mayReQueue) {
    interruptAnim = e_InterruptAnim_ReQueue;
  }

  // H3e4f-c2b-2: the simulation owns when the controller is asked for a new
  // locomotion intent, and there is at most one query per tick. The two
  // schedules share one command queue, so queries equal the union of a due tick
  // and a legacy animation opportunity rather than the sum.
  const bool legacy_opportunity = interruptAnim != e_InterruptAnim_None;
  const bool simulation_due =
      player->NoteLocomotionIntentCadence(legacy_opportunity, now, tick.ball_retainer == player);
  if (legacy_opportunity || simulation_due) {

    PlayerCommandQueue commandQueue;
    // 4b''-prep: the controller's own queue is kept separate from what SelectAnim
    // consumes, because trip handling pre-fills commandQueue with locally generated
    // commands. Publishing a locally generated fallback as a DirectMovementIntent
    // would break provenance.
    PlayerCommandQueue controllerQueue;
    bool controller_queried = false;
    const auto EnsureControllerQuery = [&]() {
      if (controller_queried) return;
      player->RequestCommand(controllerQueue,
          {tick.ball, tick.touches, tick.restart,
           tick.ball_retainer, tick.pitch, tick.own_team});
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
        player->NoteControllerQuery(trq_has, now, tick.ball_retainer == player);
      }
    };
    const bool trip_local_queue =
        interruptAnim == e_InterruptAnim_Trip && tripType != 0;

    if (trip_local_queue) {
      // ANIM-1 A7 step 2: the Player owns the local trip command construction.
      player->BuildTripSelectionCommands(
          commandQueue, tripDirection, tripType, spatialState.floatVelocity,
          spatialState.directionVec, spatialState.position);
      tripType = 0;
    }

    // Simulation cadence: adopt fresh Movement intent without waiting for
    // SelectAnim to accept it. A locally generated trip queue is not a
    // controller intent, so the tick is left overdue instead of consumed.
    if (simulation_due && !trip_local_queue) {
      EnsureControllerQuery();
    }

    if (legacy_opportunity) {
    if (legacy_opportunity && player->LocomotionIntentRefreshHeldIneligible(now, tick.ball_retainer == player)) {
      bool held_has_candidate = false;
      for (const PlayerCommand &candidate : commandQueue) {
        if (candidate.desiredFunctionType == e_FunctionType_Movement &&
            candidate.useDesiredMovement) {
          held_has_candidate = true;
          break;
        }
      }
      const e_FunctionType held_type = player->GetCurrentFunctionType();
      const bool held_retains = tick.ball_retainer == player;
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

        const PlayerCommand &command = commandQueue[i];

        found = player->TrySelectAction(now, tick, command, history, interruptAnim, false, false, true);
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
      if (player->PublishMovementIntentFromQueue(controllerQueue, now, tick.ball_retainer == player)) {
        ++HumanoidIntentRefreshes();
        player->CommitLocomotionIntentRefresh(tick.ball, now, tick.ball_retainer == player);
        ++HumanoidBasePathRefreshCommits();
      } else {
        ++HumanoidIntentCandidatesMissing();
      }
    }
    if (interruptAnim != e_InterruptAnim_ReQueue && !found) {
      const auto logger = bq::log::get_log_by_name("football");
      if (logger.is_valid()) {
        logger.warning("HumanoidBase::Process: no applicable animation; current type {}",
                       GetCurrentBakedClip().metadata.action_type);
        for (const auto& command : commandQueue) {
          logger.warning("desired animation type {}", command.desiredFunctionType);
        }
      }
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
  }
  reQueueDelayFrames = clamp(reQueueDelayFrames - 1, 0, 10000);

  interruptAnim = e_InterruptAnim_None;

  // movement/rotation smuggle

  // start with +1, because we want to influence the first frame as well
  // as for finishing, finish with frameBias = 1.0, even if the last frame is 'spiritually' the one-to-last, since the first frame of the next anim is actually 'same-tempered' as the current anim's last frame.
  // however, it works best to have all values 'done' at this one-to-last frame, so the next anim can read out these correct (new starting) values.
  float frameBias = (currentAnim.frameNum + 1) / (float)((static_cast<int>(GetCurrentBakedClip().frame_count) - 1) + 1);

  // not sure if this is correct!
  // radian beginAngle = currentAnim.rotationSmuggle.begin;// * (1.0f - frameBias); // more influence in the beginning; act more like it was 0 later on. (yes, next one is a bias within a bias) *edit: disabled, looks better without
  // currentAnim.rotationSmuggleOffset = beginAngle * (1.0f - frameBias) +
  //                                      currentAnim.rotationSmuggle.end * frameBias;

  currentAnim.rotationSmuggleOffset = currentAnim.rotationSmuggle.begin * (1.0f - frameBias) +
                                       currentAnim.rotationSmuggle.end * frameBias;

  // next frame

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
  } else {
  }

}

int HumanoidBase::GetIdleMovementAnimID() {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Movement;
  query.byIncomingVelocity = true;
  query.incomingVelocity = e_Velocity_Idle;
  query.byOutgoingVelocity = true;
  query.outgoingVelocity = e_Velocity_Idle;

  DataSet dataSet;
  BakedAnimationSelector::CrudeSelection(
      animations_.Clips(), query, dataSet);

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

void HumanoidBase::ResetPosition(const Vector3 &newPos,
                                 const Vector3 &focusPos) {

  startPos = newPos;
  startAngle = FixAngle((focusPos - newPos).GetNormalized(Vector3(0, -1, 0)).GetAngle2D());
  nextStartPos = startPos;
  nextStartAngle = startAngle;
  previousPosition2D = startPos;

  spatialState.position = startPos;
  spatialState.angle = startAngle;
  spatialState.directionVec = Vector3(0, -1, 0).GetRotated2D(startAngle);
  spatialState.floatVelocity = 0;

  spatialState.actualMovement = Vector(0);
  spatialState.physicsMovement = Vector(0);
  spatialState.animMovement = Vector(0);
  spatialState.movement = Vector(0);
  spatialState.actionSmuggleMovement = Vector(0);
  spatialState.movementSmuggleMovement = Vector(0);
  spatialState.positionOffsetMovement = Vector(0);
  spatialState.relBodyAngleNonquantized = 0;

  spatialState.enumVelocity = e_Velocity_Idle;
  spatialState.floatVelocity = 0;
  spatialState.movement = Vector3(0);
  spatialState.relBodyDirectionVec = Vector3(0, -1, 0);
  spatialState.relBodyAngle = 0;
  // After a reset the body faces the focus direction, matching the locomotion
  // direction and the animation base angle (startAngle). Otherwise the derived
  // GetFacing() (velocity=0 -> bodyFacing) would report world -Y instead of the
  // direction the reset actually asked for. relBodyDirectionVec/relBodyAngle
  // stay (0,-1,0)/0 because body and locomotion now agree.
  spatialState.bodyDirectionVec = spatialState.directionVec;
  spatialState.bodyAngle = spatialState.angle;
  spatialState.foot = e_Foot_Right;

  const AnimationId idleAnimID = GetIdleMovementAnimID();
  currentAnim.animationId = idleAnimID;
  currentAnim.positions.clear();
  currentAnim.positions = GetBakedClip(currentAnim.animationId).root_positions;
  currentAnim.frameNum =
      rng_.Uniform(0, static_cast<int>(GetCurrentBakedClip().frame_count) - 2);
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

void HumanoidBase::ApplyPositionOffset(const Vector3 &offset) {

  assert(offset.coords[2] == 0.0f);

  nextStartPos += offset;
  startPos += offset;
  spatialState.position += offset;
  spatialState.positionOffsetMovement += offset * 100.0f;
  decayingPositionOffset += offset;
  if (decayingPositionOffset.GetLength() > 0.1f) decayingPositionOffset = decayingPositionOffset.GetNormalized() * 0.1f;
  currentAnim.positionOffset += offset;
}



void HumanoidBase::ResetSituation(const Vector3 &focusPos) {
  mentalImageTime = std::chrono::milliseconds{0};
  ResetPosition(spatialState.position, focusPos);
}

bool HumanoidBase::_HighOrBouncyBall(const Ball& ball) const {
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
void HumanoidBase::_KeepBestDirectionAnims(DataSet &dataSet,
                                           const PlayerCommand &command,
                                           bool strict, radian allowedAngle,
                                           int allowedVelocitySteps,
                                           int forcedQuadrantID) {

  assert(dataSet.size() != 0);

  int bestQuadrantID = forcedQuadrantID;
  if (bestQuadrantID == -1) {

    for (auto &anim : dataSet) {
      OrderScratch(anim) = GetMovementSimilarity(anim, predicate_RelDesiredDirection, predicate_DesiredVelocity, predicate_CorneringBias);
    }
    std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareByOrderFloat, this, _1, _2));

    // we want the best anim to be a baseanim, and compare other anims to it
    if (strict) {
      if (command.desiredFunctionType != e_FunctionType_Movement) {
        std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
      }
    }

  
    bestQuadrantID = GetBakedClip(*dataSet.begin()).metadata.quadrant;
  }

  const Quadrant &quadrant = BakedAnimationSelector::GetQuadrant(bestQuadrantID);

  DataSet::iterator iter = dataSet.begin();
  iter++;
  while (iter != dataSet.end()) {

    if (strict) {
      if (GetBakedClip(*iter).metadata.quadrant == bestQuadrantID) {
        iter++;
      } else {
        iter = dataSet.erase(iter);
      }
    } else {
      int quadrantID = GetBakedClip(*iter).metadata.quadrant;
      const Quadrant &bestQuadrant = BakedAnimationSelector::GetQuadrant(bestQuadrantID);
      const Quadrant &quadrant = BakedAnimationSelector::GetQuadrant(quadrantID);

      bool predicate = true;

      if (!GetBakedClip(*iter).metadata.last_ditch) {
          // last ditch anims may always change velo
        if (std::abs(GetVelocityID(quadrant.velocity, true) - GetVelocityID(bestQuadrant.velocity, true)) > allowedVelocitySteps) predicate = false;
      }
      if (std::fabs(quadrant.angle - bestQuadrant.angle) > allowedAngle) predicate = false;

      if (predicate) {
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

  // delete nonqualified bodydir quadrants

  assert(dataSet.size() != 0);
  for (auto &anim : dataSet) {
    OrderScratch(anim) = DirectionSimilarityRating(anim);
  }
  std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&HumanoidBase::CompareByOrderFloat, this, _1, _2));

  // we want the best anim to be a baseanim, and compare other anims to it
  if (strict) {
    if (command.desiredFunctionType != e_FunctionType_Movement) {
      std::stable_sort(dataSet.begin(), dataSet.end(), std::bind(&Humanoid::CompareBaseanimSimilarity, this, _1, _2));
    }
  }

  const AnimationClip &bestClip = GetBakedClip(*dataSet.begin());

  radian bestOutgoingBodyAngle = ForceIntoAllowedBodyDirectionAngle(bestClip.metadata.outgoing_body_angle);
  radian bestOutgoingAngle = ForceIntoPreferredDirectionAngle(bestClip.metadata.outgoing_angle);
  radian bestLookAngle = bestOutgoingBodyAngle + bestOutgoingAngle;

  DataSet::iterator iter = dataSet.begin();
  iter++;
  while (iter != dataSet.end()) {

    const AnimationClip &animClip = GetBakedClip(*iter);

    radian animOutgoingBodyAngle = ForceIntoAllowedBodyDirectionAngle(animClip.metadata.outgoing_body_angle);
    radian animOutgoingAngle = ForceIntoPreferredDirectionAngle(animClip.metadata.outgoing_angle);
    radian animLookAngle = animOutgoingBodyAngle + animOutgoingAngle;

    float adaptedAllowedAngle = 0.06f * pi; // between 0 and 20 deg
    if (!strict) {
      adaptedAllowedAngle = allowedAngle;
    }
    if (std::fabs(animLookAngle - bestLookAngle) <= adaptedAllowedAngle) {
      iter++;
    } else {
      iter = dataSet.erase(iter);
    }
  }
}


void HumanoidBase::CalculatePredictedSituation(Vector3 &predictedPos,
                                               radian &predictedAngle) {

  if (currentAnim.positions.size() > (unsigned int)currentAnim.frameNum) {
    assert(currentAnim.positions.size() > (unsigned int)(static_cast<int>(GetCurrentBakedClip().frame_count) - 1));
    predictedPos = spatialState.position + currentAnim.positions.at((static_cast<int>(GetCurrentBakedClip().frame_count) - 1)) + currentAnim.actionSmuggle + currentAnim.actionSmuggleSustain + currentAnim.movementSmuggle;
  } else {
    predictedPos = spatialState.position + GetCurrentBakedClip().metadata.translation.Get2D().GetRotated2D(spatialState.angle) + currentAnim.actionSmuggle + currentAnim.actionSmuggleSustain + currentAnim.movementSmuggle;
  }

  predictedAngle = spatialState.angle + GetCurrentBakedClip().metadata.outgoing_angle + currentAnim.rotationSmuggle.end;
  predictedAngle = ModulateIntoRange(-pi, pi, predictedAngle);
  assert(predictedPos.coords[2] == 0.0f);
}

Vector3 HumanoidBase::CalculateOutgoingMovement(const std::vector<Vector3> &positions) const {
  if (positions.size() < 2) return 0;
  return (positions.at(positions.size() - 1) - positions.at(positions.size() - 2)) * 100.0f;
}

bool HumanoidBase::UsesProceduralLocomotion(const Player* ball_retainer) const {
  // Execution authority is the Player Decision Clock's current-epoch intent, not
  // the animation command. Executability is the gate; the producer contract is a
  // separate fatal oracle.
  return player && player->IsEligibleForProceduralLocomotion(ball_retainer == player) &&
         player->HasExecutableDecisionLocomotionIntent();
}


void HumanoidBase::CalculateSpatialState(const Player* ball_retainer) {
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d2: root-motion and body-pose producers are dead on this path. Foot
    // remains animation-owned gait/selection state until its separate audit.
    if (currentAnim.frameNum > 12) {
      spatialState.foot = static_cast<e_Foot>(GetCurrentBakedClip().metadata.outgoing_foot);
    }
    ++HumanoidProceduralMovementTicks();
    return;
  }
  // ANIM-1 A8 step 1: the action position and movement decomposition is a pure
  // PlayerMotion calculation; the Humanoid only supplies the clip/pose facts.
  const AnimationClip &clip = GetCurrentBakedClip();
  ActionMotionVectorInput motion_input;
  motion_input.start_position = startPos;
  motion_input.start_angle = startAngle;
  motion_input.positions = currentAnim.positions;
  motion_input.frame_num = currentAnim.frameNum;
  motion_input.action_smuggle_offset = currentAnim.actionSmuggleOffset;
  motion_input.action_smuggle_sustain_offset = currentAnim.actionSmuggleSustainOffset;
  motion_input.movement_smuggle_offset = currentAnim.movementSmuggleOffset;
  if (currentAnim.positions.size() <=
      static_cast<std::size_t>(currentAnim.frameNum)) {
    motion_input.baked_pose_position =
        clip.poses[currentAnim.frameNum].positions[BodyPart::player];
  }
  motion_input.previous_position_2d = previousPosition2D;
  motion_input.action_smuggle_movement = spatialState.actionSmuggleMovement;
  motion_input.movement_smuggle_movement = spatialState.movementSmuggleMovement;
  motion_input.position_offset_movement = spatialState.positionOffsetMovement;
  motion_input.root_positions = clip.root_positions;

  const ActionMotionVectorResult motion =
      PlayerMotion::ActionMotionVector(motion_input);
  const Vector3 position = motion.position;

  if (currentAnim.frameNum > 12) {
    spatialState.foot = static_cast<e_Foot>(clip.metadata.outgoing_foot);
  }

  spatialState.actualMovement = motion.actual_movement;
  spatialState.physicsMovement = motion.physics_movement;
  spatialState.animMovement = motion.anim_movement;
  spatialState.movement = motion.movement;
  spatialState.floatVelocity = motion.float_velocity;
  spatialState.enumVelocity = motion.enum_velocity;
  spatialState.position = position;
  if (!UsesProceduralLocomotion(ball_retainer)) {
    ++HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement();
  Vector3 bodyPosition;
  Quaternion bodyOrientation;
  const auto &bodyPose = GetCurrentBakedClip().poses[currentAnim.frameNum];
  bodyOrientation = bodyPose.orientations[body];
  bodyPosition = bodyPose.positions[body];
  real x, y, z;
  bodyOrientation.GetAngles(x, y, z);

  Vector3 quatDirection; quatDirection = bodyOrientation;

  Vector3 bodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(z + startAngle + currentAnim.rotationSmuggleOffset);

  spatialState.floatVelocity = spatialState.movement.GetLength();
  spatialState.enumVelocity = FloatToEnumVelocity(spatialState.floatVelocity);

  if (spatialState.enumVelocity != e_Velocity_Idle) {
    spatialState.directionVec = spatialState.movement.GetNormalized();
  } else {
    // too slow for comfort, use body direction as global direction
    spatialState.directionVec = bodyDirectionVec;
  }

  spatialState.position = position;
  spatialState.angle = ModulateIntoRange(-pi, pi, FixAngle(spatialState.directionVec.GetAngle2D()));

  if (spatialState.enumVelocity != e_Velocity_Idle) {
    Vector3 adaptedBodyDirectionVec = bodyDirectionVec.GetRotated2D(-spatialState.angle);
    // prefer straight forward, so lie about the actual direction a bit
    // this may fix bugs of body dir being non-0 somewhere during 0 anims
    // but it may also cause other bugs (going 0 to 45 all over again each time)
    //adaptedBodyDirectionVec = (adaptedBodyDirectionVec * 0.95f + Vector3(0, -1, 0) * 0.05f).GetNormalized(0);
    //ForceIntoAllowedBodyDirectionVec(Vector3(0, -1, 0)).Print();

    bool preferCorrectVeloOverCorrectAngle = true;
    radian bodyAngleRel = adaptedBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
    if (spatialState.enumVelocity == e_Velocity_Sprint &&
        std::fabs(bodyAngleRel) >= 0.125f * pi) {
      if (preferCorrectVeloOverCorrectAngle) {
        // on impossible combinations of velocity and body angle, decrease body angle
        adaptedBodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(0.12f * pi * signSide(bodyAngleRel));
      } else {
        // on impossible combinations of velocity and body angle, decrease velocity
        spatialState.floatVelocity = walkSprintSwitch - 0.1f;
        spatialState.enumVelocity = FloatToEnumVelocity(spatialState.floatVelocity);
      }
    } else if (spatialState.enumVelocity == e_Velocity_Walk &&
               std::fabs(bodyAngleRel) >= 0.5f * pi) {
      if (preferCorrectVeloOverCorrectAngle) {
        // on impossible combinations of velocity and body angle, decrease body angle
        adaptedBodyDirectionVec = Vector3(0, -1, 0).GetRotated2D(0.495f * pi * signSide(bodyAngleRel));
      } else {
        // on impossible combinations of velocity and body angle, decrease velocity
        spatialState.floatVelocity = dribbleWalkSwitch - 0.1f;
        spatialState.enumVelocity = FloatToEnumVelocity(spatialState.floatVelocity);
      }
    }

    spatialState.relBodyDirectionVecNonquantized = adaptedBodyDirectionVec;
    spatialState.relBodyDirectionVec = ForceIntoAllowedBodyDirectionVec(adaptedBodyDirectionVec);
  } else {
    spatialState.relBodyDirectionVecNonquantized = Vector3(0, -1, 0);
    spatialState.relBodyDirectionVec = Vector3(0, -1, 0);
  }
  spatialState.relBodyAngle = spatialState.relBodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  spatialState.relBodyAngleNonquantized = spatialState.relBodyDirectionVecNonquantized.GetAngle2D(Vector3(0, -1, 0));
  spatialState.bodyDirectionVec = spatialState.relBodyDirectionVec.GetRotated2D(spatialState.angle); // rotate back, we now have it forced into allowed angle
  spatialState.bodyAngle = spatialState.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  }

  previousPosition2D = position;

  // The kinematic mirror must track the spatial state at the instant it
  // changes. Controllers are queried later in the same tick and read the
  // player's own position/movement through Player, so a mirror that only
  // refreshed at the end of Player::Process would be a tick behind.
  if (player) player->CommitLegacyActionMotion();
}

void HumanoidBase::CalculateFactualSpatialState() {

  spatialState.foot = static_cast<e_Foot>(GetCurrentBakedClip().metadata.outgoing_foot);

  if (!GetCurrentBakedClip().metadata.outgoing_special_state.empty()) {
    spatialState.floatVelocity = 0;
    spatialState.enumVelocity = e_Velocity_Idle;
    spatialState.movement = Vector3(0);
  }

  if (player) player->CommitLegacyActionMotion();
}

void HumanoidBase::ProjectSimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  spatialState.position = state.position;
  spatialState.movement = state.velocity;
  if (UsesProceduralLocomotion(ball_retainer)) {
    // H3e4d1: these serialized legacy aliases have no pure-locomotion producer.
    // Project the authoritative velocity rather than retaining animation history.
    spatialState.actualMovement = state.velocity;
    spatialState.physicsMovement = state.velocity;
    spatialState.animMovement = state.velocity;
  }
  spatialState.directionVec = state.GetFacing();
  spatialState.floatVelocity = spatialState.movement.GetLength();
  spatialState.enumVelocity = FloatToEnumVelocity(spatialState.floatVelocity);
  spatialState.angle =
      ModulateIntoRange(-pi, pi, FixAngle(spatialState.directionVec.GetAngle2D()));
  ApplySimulationBodyState(state);
  // The next tick derives its raw movement from this, so it must describe the
  // position actually in force. Collision corrections land here too, exactly
  // as they did when the legacy root motion owned the position.
  previousPosition2D = spatialState.position;
}

void HumanoidBase::ApplySimulationMovementState(
    const PlayerKinematicState &state, const Player* ball_retainer) {
  // Legacy action path: animation root motion produced this state, so adopting
  // it as the Player's motion is correct here -- and is the only remaining
  // source-explicit way for the Humanoid to write the Player.
  ProjectSimulationMovementState(state, ball_retainer);
  if (player) player->CommitLegacyActionMotion();
}

void HumanoidBase::ApplySimulationBodyState(
    const PlayerKinematicState &state) {
  // Continuous simulation truth. Nothing in this function writes back into
  // state.bodyFacing, so legacy quantization cannot corrupt torso authority.
  spatialState.bodyDirectionVec = state.bodyFacing;
  spatialState.bodyAngle =
      spatialState.bodyDirectionVec.GetAngle2D(Vector3(0, -1, 0));
  const Vector3 relative = state.bodyFacing.GetRotated2D(-spatialState.angle)
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
    const PlayerKinematicState &tickStartState, const Player* ball_retainer,
    bool restart_needs_simulation) {
  if (!player) return;
  if (!UsesProceduralLocomotion(ball_retainer)) {
    // Non-locomotion ticks keep the legacy animation root motion. Projecting
    // it keeps the Humanoid movement fields a single projection of the
    // simulation kinematic state.
    ApplySimulationMovementState(player->GetKinematicState(), ball_retainer);
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
  // P2a: Player computes the locomotion through PlayerMotion and commits the
  // resulting state. S0.1: this path only *projects* it into the legacy spatial
  // state -- it does not adopt it back, so the Humanoid can no longer overwrite
  // a locomotion tick the Player already owns. The oracle proves the two sides
  // agree at this point.
  const PlayerKinematicState next = player->CommitLocomotion(
      command, tickStartState, restart_needs_simulation, 0.01f);
  ProjectSimulationMovementState(next, ball_retainer);
  player->CheckSimulationKinematicOracle();
}


void HumanoidBase::SetFootSimilarityPredicate(e_Foot desiredFoot) const {
  predicate_DesiredFoot = desiredFoot;
}

bool HumanoidBase::CompareFootSimilarity(e_Foot foot, int animIndex1, int animIndex2) const {
  int one = 1;
  int two = 1;
  if (static_cast<e_Foot>(GetBakedClip(animIndex1).metadata.current_foot) == predicate_DesiredFoot) one = 0;
  if (static_cast<e_Foot>(GetBakedClip(animIndex2).metadata.current_foot) == predicate_DesiredFoot) two = 0;
  if (FloatToEnumVelocity(GetBakedClip(animIndex1).metadata.incoming_velocity) == e_Velocity_Idle) one = 0;
  if (FloatToEnumVelocity(GetBakedClip(animIndex2).metadata.incoming_velocity) == e_Velocity_Idle) two = 0;
  return one < two;
}

void HumanoidBase::SetIncomingVelocitySimilarityPredicate(e_Velocity velocity) const {
  predicate_IncomingVelocity = velocity;
}

bool HumanoidBase::CompareIncomingVelocitySimilarity(int animIndex1, int animIndex2) const {
  /* old version
  float rating1 = std::fabs(clamp(RangeVelocity(GetBakedClip(animIndex1).metadata.incoming_velocity) - EnumToFloatVelocity(predicate_IncomingVelocity), -sprintVelocity, sprintVelocity));
  float rating2 = std::fabs(clamp(RangeVelocity(GetBakedClip(animIndex2).metadata.incoming_velocity) - EnumToFloatVelocity(predicate_IncomingVelocity), -sprintVelocity, sprintVelocity));
  */

  int currentVelocityID = GetVelocityID(predicate_IncomingVelocity);

  // rate difference anim incoming / actual incoming
  int anim1_incomingVelocityID = GetVelocityID(FloatToEnumVelocity(GetBakedClip(animIndex1).metadata.incoming_velocity));
  int anim2_incomingVelocityID = GetVelocityID(FloatToEnumVelocity(GetBakedClip(animIndex2).metadata.incoming_velocity));
  float rating1 = std::fabs(clamp(anim1_incomingVelocityID - currentVelocityID, -3, 3));
  float rating2 = std::fabs(clamp(anim2_incomingVelocityID - currentVelocityID, -3, 3));

  // also add a penalty for anim incoming velocities which aren't between actual incoming and anim outgoing
  int anim1_outgoingVelocityID = GetVelocityID(FloatToEnumVelocity(GetBakedClip(animIndex1).metadata.outgoing_velocity));
  int anim2_outgoingVelocityID = GetVelocityID(FloatToEnumVelocity(GetBakedClip(animIndex2).metadata.outgoing_velocity));
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

  Vector3 outgoingDirection = ForceIntoPreferredDirectionVec(GetBakedClip(animIndex).metadata.outgoing_direction);
  float outgoingVelocity = RangeVelocity(GetBakedClip(animIndex).metadata.outgoing_velocity);
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
  float rating1 = OrderScratch(animIndex1);
  float rating2 = OrderScratch(animIndex2);
  return rating1 < rating2;
}

void HumanoidBase::SetIncomingBodyDirectionSimilarityPredicate(const Vector3 &relIncomingBodyDirection) const {
  predicate_RelIncomingBodyDirection = relIncomingBodyDirection;
}

bool HumanoidBase::CompareIncomingBodyDirectionSimilarity(int animIndex1, int animIndex2) const {
  float rating1 = std::fabs(ForceIntoAllowedBodyDirectionVec(GetBakedClip(animIndex1).metadata.incoming_body_direction).GetAngle2D(ForceIntoAllowedBodyDirectionVec(predicate_RelIncomingBodyDirection))) / pi;
  float rating2 = std::fabs(ForceIntoAllowedBodyDirectionVec(GetBakedClip(animIndex2).metadata.incoming_body_direction).GetAngle2D(ForceIntoAllowedBodyDirectionVec(predicate_RelIncomingBodyDirection))) / pi;
  if (FloatToEnumVelocity(GetBakedClip(animIndex1).metadata.incoming_velocity) == e_Velocity_Idle) rating1 = 0;//-1;
  if (FloatToEnumVelocity(GetBakedClip(animIndex2).metadata.incoming_velocity) == e_Velocity_Idle) rating2 = 0;//-1;

  return rating1 < rating2;
}

void HumanoidBase::SetBodyDirectionSimilarityPredicate(const Vector3 &lookAt) const {
  predicate_LookAt = lookAt;
}

real HumanoidBase::DirectionSimilarityRating(int animIndex) const {
  const AnimationClip &a1Clip = GetBakedClip(animIndex);
  Vector3 relDesiredBodyDirection1 = ((predicate_LookAt - spatialState.position).GetRotated2D(-spatialState.angle) - a1Clip.metadata.translation).GetNormalized(Vector3(0, -1, 0));
  radian maxAngleSmuggle = 0.1f * pi;
  radian outgoingAngle1 = a1Clip.metadata.outgoing_direction.GetRotated2D( clamp(predicate_RelDesiredDirection.GetAngle2D(a1Clip.metadata.outgoing_direction), -maxAngleSmuggle, maxAngleSmuggle) ).GetAngle2D(Vector3(0, -1, 0));
  Vector3 predictedOutgoingBodyDirection1 = Vector3(0, -1, 0).GetRotated2D(a1Clip.metadata.outgoing_body_angle).GetRotated2D(outgoingAngle1);
  radian rating1 = std::fabs(predictedOutgoingBodyDirection1.GetAngle2D(relDesiredBodyDirection1));
  // penalty for body angles (as opposed to straight forward), to get a slight preference for forward angles
  rating1 += std::fabs(a1Clip.metadata.outgoing_body_angle) * 0.05f;
  return rating1;
}

bool HumanoidBase::CompareBodyDirectionSimilarity(int animIndex1, int animIndex2) const {
  return DirectionSimilarityRating(animIndex1) < DirectionSimilarityRating(animIndex2);
}

void HumanoidBase::SetTripDirectionSimilarityPredicate(const Vector3 &relDesiredTripDirection) const {
  predicate_RelDesiredTripDirection = relDesiredTripDirection;
}

bool HumanoidBase::CompareTripDirectionSimilarity(int animIndex1, int animIndex2) const {
  float rating1 = -GetBakedClip(animIndex1).metadata.bump_direction.GetDotProduct(predicate_RelDesiredTripDirection);
  float rating2 = -GetBakedClip(animIndex2).metadata.bump_direction.GetDotProduct(predicate_RelDesiredTripDirection);
  return rating1 < rating2;
}

bool HumanoidBase::CompareBaseanimSimilarity(int animIndex1, int animIndex2) const {
  bool isBase1 = GetBakedClip(animIndex1).metadata.base_animation;
  bool isBase2 = GetBakedClip(animIndex2).metadata.base_animation;

  if (isBase1 == true && isBase2 == false) return true;
  return false;
}

bool HumanoidBase::CompareCatchOrDeflect(int animIndex1, int animIndex2) const {
  bool catch1 = (GetBakedClip(animIndex1).metadata.outgoing_retain_state != "");
  bool catch2 = (GetBakedClip(animIndex2).metadata.outgoing_retain_state != "");

  if (catch1 == true && catch2 == false) return true;
  return false;
}

void HumanoidBase::SetIdlePredicate(float desiredValue) const {
  predicate_idle = desiredValue;
}

bool HumanoidBase::CompareIdleVariable(int animIndex1, int animIndex2) const {
  return std::fabs(GetBakedClip(animIndex1).metadata.idle_level - predicate_idle) <
         std::fabs(GetBakedClip(animIndex2).metadata.idle_level - predicate_idle);
}

bool HumanoidBase::ComparePriorityVariable(int animIndex1, int animIndex2) const {
  return std::fabs(GetBakedClip(animIndex1).metadata.priority) <
         std::fabs(GetBakedClip(animIndex2).metadata.priority);
}

Vector3 HumanoidBase::CalculatePhysicsVector(football::sim::Tick now, int animID, bool useDesiredMovement, const Vector3 &desiredMovement, bool useDesiredBodyDirection, const Vector3 &desiredBodyDirectionRel, std::vector<Vector3> &positions_ret, radian &rotationOffset_ret) const {
  // ANIM-1 A6.4 A8.0: the candidate reference-motion kernel now lives in
  // PlayerMotion as a pure function. This adapter only assembles the explicit
  // kinematic/capability inputs and writes the plan back; the algorithm itself
  // is unchanged (see PlayerMotion::BuildActionMotionPlan).
  const AnimationClip &clip = GetBakedClip(animID);

  ActionMotionInput input;
  input.spatial_movement = spatialState.movement;
  input.angle = spatialState.angle;
  input.direction_vec = spatialState.directionVec;
  input.float_velocity = spatialState.floatVelocity;
  input.use_desired_movement = useDesiredMovement;
  input.desired_movement = desiredMovement;
  input.use_desired_body_direction = useDesiredBodyDirection;
  input.desired_body_direction_rel = desiredBodyDirectionRel;
  input.parameters.max_velocity = player->GetMaxVelocity();
  input.parameters.stat_agility = player->GetStat(football::player::PlayerStat::physical_agility);
  input.parameters.stat_acceleration = player->GetStat(football::player::PlayerStat::physical_acceleration);
  input.parameters.stat_velocity = player->GetStat(football::player::PlayerStat::physical_velocity);
  input.parameters.stat_dribble = player->GetStat(football::player::PlayerStat::technical_dribble);
  input.parameters.stat_balance = player->GetStat(football::player::PlayerStat::physical_balance);
  input.parameters.stat_technical_ballcontrol = player->GetStat(football::player::PlayerStat::technical_ballcontrol);
  input.parameters.last_touch_bias_1000 = player->GetLastTouchBias(1000, now);
  input.parameters.decaying_position_offset_length = decayingPositionOffset.GetLength();

  const ActionMotionPlan plan = PlayerMotion::BuildActionMotionPlan(clip, input);

  positions_ret = plan.positions;
  rotationOffset_ret = plan.rotation_offset;
  return plan.outgoing_velocity;
}


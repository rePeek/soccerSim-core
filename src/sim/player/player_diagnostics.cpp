// ANIM-1 A9.9: library-side diagnostic definitions.
//
// These were defined in HumanoidBase's translation unit only because the
// Humanoid was the code that incremented them. They are measurement-only
// counters and provenance helpers, never simulation state, so they now live in
// their own translation unit. Definitions are verbatim moves.
#include "sim/player/player_diagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include "football/animation/library.hpp"
#include "football/animation/movement_math.hpp"
#include "football/animation/selector.hpp"

using namespace football::math;

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


// ANIM-2 A11.0: body-forecast dual-run comparison counters.
int &BakedBodyPreviewChecks() { static int value = 0; return value; }

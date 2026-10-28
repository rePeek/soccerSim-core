// PEX-4: implementations of the execution's migration/audit telemetry.
//
// Storage scope is unchanged (process-global, see player_diagnostics.hpp): this
// move only takes the declarations out of the Player entity header and puts
// every writer next to the executor that owns the measurement.
#include "player/execution/player_execution_diagnostics.hpp"

#include <cstdio>

namespace {

struct ControllerQueryOpportunityAudit {
  int queries = 0;
  int with_candidate = 0;
  int candidate_not_due = 0;
  int candidate_due_ineligible = 0;
  int candidate_due_eligible = 0;
  int no_candidate_not_due = 0;
  int no_candidate_other = 0;
};

ControllerQueryOpportunityAudit &OpportunityAudit() {
  static ControllerQueryOpportunityAudit audit;
  return audit;
}

}  // namespace

int &SimulationOnlyGateMismatchForResetContext(int context) {
  static int records[kResetSituationCallContextCount] = {};
  return records[context];
}

const char *ResetSituationCallContextName(int context) {
  switch (context) {
    case kResetSituationInitialBeforeFirstPlayerTick: return "initial";
    case kResetSituationRuntime: return "runtime";
    case kResetSituationPlayerDeactivateFirst: return "player_deactivate_first";
    case kResetSituationPlayerDeactivateSecond: return "player_deactivate_second";
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

void RecordControllerQueryOpportunity(bool had_movement_candidate, bool due,
                                      bool eligible) {
  ControllerQueryOpportunityAudit &audit = OpportunityAudit();
  ++audit.queries;
  if (had_movement_candidate) {
    ++audit.with_candidate;
    if (!due) ++audit.candidate_not_due;
    else if (!eligible) ++audit.candidate_due_ineligible;
    else ++audit.candidate_due_eligible;
  } else {
    if (!due) ++audit.no_candidate_not_due;
    else ++audit.no_candidate_other;
  }
}

void DumpQueryOpportunities() {
  const ControllerQueryOpportunityAudit &audit = OpportunityAudit();
  printf("4opp-SUMMARY queries=%d with_candidate=%d\n", audit.queries,
         audit.with_candidate);
  printf("4opp-SUMMARY candidate: not_due=%d due+ineligible=%d due+eligible=%d\n",
         audit.candidate_not_due, audit.candidate_due_ineligible,
         audit.candidate_due_eligible);
  printf("4opp-SUMMARY no_candidate: not_due=%d other=%d\n",
         audit.no_candidate_not_due, audit.no_candidate_other);
  fflush(stdout);
}

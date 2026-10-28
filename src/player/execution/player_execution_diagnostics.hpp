// PEX-4: execution diagnostics, moved out of the Player entity header.
//
// These are measurement-only counters and provenance records for the
// locomotion/decision migration. They are process-global telemetry (see
// player_diagnostics.hpp for the storage scope note), never gameplay state, and
// the Player entity no longer declares or depends on them. The tests and tools
// that read them include this header; the Player's own public diagnostic
// accessors (install/authority counters) are forwarded by Player and unchanged.
#ifndef FOOTBALL_SIM_PLAYER_EXECUTION_DIAGNOSTICS_HPP
#define FOOTBALL_SIM_PLAYER_EXECUTION_DIAGNOSTICS_HPP

#include <optional>

#include "football/foundation/time/tick.hpp"
#include "football/foundation/time/tick_boundary.hpp"

// Caller scope for the ResetSituation instrumentation. This is observation
// only; Deactivate's double reset is deliberately kept.
enum ResetSituationCallContext {
  kResetSituationUnspecified = 0,
  kResetSituationInitialBeforeFirstPlayerTick,
  kResetSituationRuntime,
  kResetSituationPlayerDeactivateFirst,
  kResetSituationPlayerDeactivateSecond,
  kResetSituationCallContextCount
};
const char *ResetSituationCallContextName(int context);
int &SimulationOnlyGateMismatchForResetContext(int context);

// Diagnostic dump for the "opportunity" census (stdout, telemetry only).
void DumpQueryOpportunities();
// One controller-query opportunity, classified by cadence and eligibility.
void RecordControllerQueryOpportunity(bool had_movement_candidate, bool due,
                                      bool eligible);

int &DecisionLocomotionIntentPresentTicks();
int &DecisionLocomotionIntentMissingTicks();
int &DecisionLocomotionIntentMissingForSource(int source);
// H3e4f-g0b-oracle-source-policy: which source contract each locomotion
// consumption is checked against, so removing a fatal comparison can be told
// apart from deleting the check.
int &MovementOracleConsumesForSource(int source);
int &ActionCoupledLegacyMismatch();
int &DecisionLocomotionIntentPresentTicksLegacyGateFalse();
int &DecisionLocomotionIntentMissingTicksLegacyGateFalse();
int &DecisionLocomotionIntentMissingForSourceLegacyGateFalse(int source);
// H3e4f-g0b-reentry-audit: continuous locomotion may hold an intent, but a
// re-entry after leaving locomotion must not silently reuse it. Categories:
// 0 initial, 1 continuous, 2 action re-entry, 3 reset re-entry.
struct LocomotionReentryAudit {
  int ticks = 0;
  int generation_advanced = 0;
  int generation_unchanged = 0;
  int scheduler_due = 0;
  int scheduler_not_due = 0;
  football::sim::TickSpan decision_age_sum{};
  int decision_age_count = 0;
  std::optional<football::sim::TickSpan> decision_age_max;
  // Shadow of the epoch rule: this epoch has no publication yet, so a decision
  // would be required before this locomotion state could be executed.
  int would_require_fresh = 0;
  // Attribution of a stale epoch: if the last reset is newer than the last
  // publication, the reset advanced the epoch and no decision has caught up.
  int stale_after_reset = 0;
  // A stale epoch observed on action re-entry is expected to force continuity repair.
  int stale_after_action_exit = 0;
  int stale_not_explained_by_reset = 0;
};
LocomotionReentryAudit &LocomotionReentryAuditFor(int category);
int &LocomotionActionExitCount();
// Publication cause split, derived without touching the player path: a
// publication on a tick where the simulation cadence was not due can only have
// come from the animation lifecycle's query opportunity.
int &DecisionPublicationViaSimulationCadence();
int &DecisionPublicationViaLegacyOpportunityOnly();
int &DecisionPublicationWhileIneligible();
int &ReentryFreshViaSimulationCadence();
int &ReentryFreshViaLegacyOpportunityOnly();
int &ReentryFreshWhileIneligible();
int &LocomotionNegativeDecisionAgeSamples();
// step 2: continuity repair trigger telemetry. attempts records the predicate
// firing, so a repair on a non-stale epoch would show as a widened trigger.
int &ContinuityRepairAttempts();
int &ContinuityRepairPublications();
int &ContinuityRepairCandidatesMissing();
// Explicit publication cause, so a repair publication is not mislabelled as an
// animation-driven one. 0 cadence, 1 legacy opportunity only, 2 continuity repair.
int &DecisionPublicationCauseCount(int cause);
int &LocomotionReentryMeasurementEpoch();
void ResetLocomotionReentryAudits();
const char *LocomotionReentryCategoryName(int category);

#endif  // FOOTBALL_SIM_PLAYER_EXECUTION_DIAGNOSTICS_HPP

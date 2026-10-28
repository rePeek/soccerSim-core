// ANIM-1 A9.4: library-side diagnostics that used to live in HumanoidBase's
// header.
//
// These are measurement-only counters and provenance structs, shared with the
// regression harness and never simulation state. They lived in the Humanoid
// header only because the Humanoid was the code that incremented them. The
// Player is now their owner, so they are declared here without depending on a
// Humanoid type.
//
// P19.5 storage scope (audit result; the scope is not changed yet):
//   - process-global telemetry: every `int&`/`vector&` accessor below is a
//     file-static single instance in player_diagnostics.cpp. It aggregates all
//     matches in the process, so a future parallel-match runner must not share
//     it across simulations; promotion to a Simulation-scoped census is the
//     remaining step and is deliberately not bundled with P19.
//   - per-Player state: the Player's own counters and the private
//     anim_state_/spatial_state_ compatibility projection. Those have no writer
//     outside the Player, so they are already single-owner.
//   - per-Simulation diagnostics: the Simulation-owned shadow reports
//     (body/active-touch/snapshot) live in Simulation, not here.
#ifndef FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP

#include <vector>

#include "player/command/player_command.hpp"
#include "football/player/player_definition.hpp"
#include "player/command/player_interrupt.hpp"

namespace football::sim { class PlayerRuntimeSink; }
namespace football::sim::player { class PlayerTouchSink; }

using namespace football::math;

class AnimationLibrary;
class Player;

// Library-side H3e4b diagnostics; shared with regression, never simulation state.
int &HumanoidProceduralMovementTicks();
// LOCO-2.3c: reference playback-rate recording only; the integer frameNum
// clock is NOT driven by these values until LOCO-2.6.
struct LocomotionPlaybackAudit {
  int samples = 0;
  float reference_speed_sum = 0.0f;
  float playback_rate_sum = 0.0f;
  float playback_rate_max = 0.0f;
  // ANIM-1 A6.3a Movement shadow oracle: on each pure-Movement selection the
  // motion-driven strategy's choice is compared with the legacy candidate set.
  int shadow_checks = 0;
  int shadow_in_candidates = 0;
  int shadow_not_in_candidates = 0;
  // ANIM-1 A6.3b: final-result comparison (the chosen Clip, not just candidate
  // membership). `fallback_count` is the ticks where the legacy fallback pick
  // was used because the motion-driven choice was not a legal candidate.
  int clip_matches = 0;
  int clip_mismatches = 0;
  int fallback_count = 0;
  // ANIM-1 A6.3b.2: the pure Animation refinement vs the legacy filter+sort
  // result, compared as an ordered sequence.
  int refine_checks = 0;
  int refine_matches = 0;
  int refine_mismatches = 0;
  // ANIM-1 A6.4 step 1b: non-Movement pure refinement vs the legacy result.
  int nm_pure_ticks = 0;
};
LocomotionPlaybackAudit &LocomotionPlaybackAuditState();
int &HumanoidLegacyBodyPoseSamplesOnProceduralMovement();
int &HumanoidLegacyBodyPoseSamplesOnNonProceduralMovement();
// reset-seed-prep: compare animation and simulation locomotion gates while the
// legacy gate remains active. Count only action-eligible actor evaluations.
int &HumanoidLocomotionGateBothTrue();
int &HumanoidLocomotionGateLegacyOnly();
int &HumanoidLocomotionGateSimulationOnly();
int &HumanoidLocomotionGateBothFalse();
// Once-per-real-Player-tick mismatch provenance (unlike the evaluation counters
// above, this is sampled only at Humanoid::Process tick start).
struct PlayerTickGateMismatchContext {
  int ticks = 0;
  int simulation_source[5] = {};
  int legacy_uninitialized = 0;
  int legacy_non_movement = 0;
  int legacy_movement_without_desired = 0;
  int legacy_other = 0;
  long direct_age_sum_ms = 0;
  int direct_age_count = 0;
  int direct_age_max_ms = -1;
  long reset_age_sum_ms = 0;
  int reset_age_count = 0;
  int reset_age_max_ms = -1;
};
PlayerTickGateMismatchContext &PlayerTickGateMismatchFor(bool simulation_only);
void RecordPlayerTickGateMismatch(bool legacy_gate, bool simulation_gate,
                                  bool initialized, int source,
                                  e_FunctionType command_type,
                                  bool command_uses_desired_movement,
                                  int direct_age_ms, int reset_age_ms);
// Diagnostics for the movement-command shadow (library-side single instance).
int &PlayerMovementCommandNonMovementTicks();
int &PlayerMovementCommandDirectAdoptions();
int &PlayerLocomotionIntentDueTicks();
int &PlayerLocomotionCadenceSelectionSuppressed();
int &PlayerLocomotionIntentLegacyOpportunityTicks();
int &PlayerLocomotionIntentOverlapTicks();
// c2a2: the parts of the due count that the flipped scheduler must NOT consume,
// because a non-locomotion action owns the tick and its intent would be stale.
int &PlayerLocomotionIntentDueIneligibleTicks();
int &PlayerLocomotionIntentConsumedTicks();
int &HumanoidEligibilityGainRefreshes();
int &HumanoidEligibilityGainCandidatesMissing();
// Telemetry for the football-player processing path.
int &PlayerPathControllerQueries();
int &PlayerPathQueriesWithMovement();
int &PlayerPathQueriesWithMovementSuppressedByRepair();
int &PlayerPathDirectPublications();
int &PlayerPathRefreshCommits();
int &PlayerPathCandidatesMissing();
int &HumanoidBasePathRefreshCommits();
// 4b''-player-noquery-audit: the no-controller-query Trip fallback is kept
// separate from DirectMovementIntent. These are measurement-only counters.
int &PlayerPathLocalTripAttempts();
int &PlayerPathLocalTripSelected();
int &PlayerPathLocalTripMovementFallbackSelected();
int &PlayerPathLocalTripMovementFallbackRefreshCommits();
// 4f-a1: how much animation-owned query pressure still reaches the Player
// Decision Clock. A legacy-only opportunity is a requeue on a tick where the
// simulation cadence was not due, so any query or publication there is caused
// by the animation lifecycle rather than by the simulation clock.
int &LegacyOnlyDecisionOpportunities();
int &LegacyOnlyDecisionQueries();
int &LegacyOnlyDecisionMovementQueries();
int &LegacyOnlyDecisionPublications();
int &LegacyOnlyDecisionMaterialChanges();
int &LegacyOnlyDecisionMovementSelectionPublications();
int &LegacyOnlyDecisionMovementSelectionMaterialChanges();
int &LegacyOnlyDecisionMovementSelections();
int &LegacyOnlyDecisionNonMovementSelections();
int &LegacyOnlyDecisionNoSelection();
// 4f-a2: whether two commands would drive SelectAnim identically. Floats and
// vectors compare bit-exact.
bool PlayerCommandsDecisionEqual(const PlayerCommand &a, const PlayerCommand &b);
// 4f-a2: can the animation requeue consume the last simulation-owned decision
// queue instead of querying? Counted only on legacy-caused queries, and split
// into proven/unproven because only a matching prefix is a proof.
int &LegacyOnlyDecisionCausedQueries();
int &SimulationDecisionCachePresent();
int &SimulationDecisionCacheMissing();
std::vector<int> &SimulationDecisionCacheAge_ms();
int &SimulationDecisionLiveHasMovement();
int &SimulationDecisionCacheHasMovement();
int &SimulationDecisionMovementEqual();
int &SimulationDecisionMovementDifferent();
int &SimulationDecisionQueueIdentical();
std::vector<int> &SimulationDecisionFirstDiffIndex();
int &SimulationDecisionProofMovementProven();
int &SimulationDecisionProofMovementUnproven();
int &SimulationDecisionProofActionProven();
int &SimulationDecisionProofActionUnproven();
int &SimulationDecisionProofNoneProven();
int &SimulationDecisionProofNoneUnproven();

// 4f-a3-prep/a3a: interval provenance for real RequestCommand calls.
enum class PlayerDecisionQueryCause {
  LegacyCaused,
  LocomotionCadence,
  ContinuityRepair,
  PlayerDecisionClockPeriodic
};
void ResetPlayerDecisionCadenceTelemetry();
void RecordPlayerDecisionQuery(const void *player_key,
                               const PlayerCommandQueue &commands, int now_ms,
                               PlayerDecisionQueryCause cause,
                               e_FunctionType action_type);
void DumpPlayerDecisionCadenceTelemetry();
// 4f-a3a: serialized simulation-owned full decision-clock telemetry.
int &PlayerDecisionClockQueries();
int &PlayerDecisionClockPeriodicQueries();
int &PlayerDecisionClockForcedQueries();
int &PlayerDecisionClockQueueConsumersMissing();
// 4b': separate intent-refresh eligibility from execution eligibility.
int &HeldDueMovementRetainsTicks();
int &HeldDueMovementRetainsCandidate();
int &HeldDueBallControlTicks();
int &HeldDueBallControlCandidate();
int &HeldDueTrapTicks();
int &HeldDueTrapCandidate();
int &HeldDueOtherTicks();
int &HeldDueOtherCandidate();
int &HumanoidIntentRefreshes();
int &HumanoidIntentCandidatesMissing();
int &HumanoidIntentRefreshCommits();
int &DirectVsLegacyCommandEqual();
int &DirectVsLegacyCommandMateriallyDifferent();

// H3e4e1 diagnostics: who owns the movement command lifetime right now. Every
// accepted Movement command still arrives through an animation selection, so
// these counters measure the scheduler we will have to replace, not the
// locomotion model. Library-side single instances for the usual reason.
int &HumanoidSchedulerQueries();
int &HumanoidMaterialCommandCandidates();
int &HumanoidMaterialCommandsAccepted();
int &HumanoidMovementSelections();
int &HumanoidMovementSwitches();
int &HumanoidMovementRequeues();
int &HumanoidMovementFromOtherAction();
std::vector<int> &HumanoidMovementCommandLifetimes_ms();
std::vector<int> &HumanoidSelectedMovementFrames();
// Same criterion the movement scheduler itself uses to call a candidate "too
// similar to what we are already trying to accomplish" (1.5 m/s momentum gap).
bool MovementCommandDiffersMaterially(const PlayerCommand &in_force,
                                      const PlayerCommand &candidate);
// H3e4e2: how much does the foot tie-break actually move the final order? The
// counterfactual clones the candidate set before the foot stable_sort and then
// receives exactly the same remaining sorts, so the only difference is foot.
int &HumanoidFootCounterfactualSelections();
int &HumanoidFootWinnerChanged();
int &HumanoidFootFrameCountDiff();
int &HumanoidFootQuadrantDiff();
int &HumanoidFootOutgoingVelocityDiff();
int &HumanoidFootOutgoingAngleBitsDiff();
int &HumanoidFootOutgoingAngleBucketDiff();
int &HumanoidFootSpecialStateDiff();
int &HumanoidFootLifecycleChanged();
void RecordFootCounterfactual(const AnimationLibrary& animations, int with_foot_head,
                              int without_foot_head);
// 4f-c diagnostic A/B hook. Transient and disabled in normal gameplay; a
// restored branch can choose the alternative Movement foot order exactly once.
struct MovementAnimationPerturbation {
  bool enabled = false;
  bool require_frame_count_difference = false;
  bool applied = false;
  football::player::PlayerId player_id = football::player::kInvalidPlayerId;
  int time_ms = -1;
  int original_anim_id = -1;
  int alternative_anim_id = -1;
};
MovementAnimationPerturbation &MovementAnimationPerturbationAudit();
// 5a1: transient observation, never used to decide contact or serialized.
// A football::ball::Ball::Touch request is not proof that football::ball::Ball physics adopted that impulse.
struct ContactAuthorityAudit {
  int scheduled = 0;
  int at_contact_frame = 0;
  int distance_rejected = 0;
  int height_rejected = 0;
  int physically_reachable = 0;
  int impulse_calls = 0;
  int nonzero_impulse_requests = 0;
  int suppressed = 0;
  int reachable_without_impulse = 0;
  int pass_fiddling = 0;
  int knock_on = 0;
  int command_target = 0;
  int forced_target = 0;
  int incoming_retain_override = 0;
  int max_power_profile_present = 0;
  int difficulty_profile_present = 0;
  int native_ball_direction_present = 0;
  int contact_bodypart_present = 0;
  int incoming_retain_present = 0;
  int outgoing_retain_present = 0;
  int contact_position_offset_nonzero = 0;
  int animation_positions_present = 0;
  std::vector<int> observed_elapsed_ms;
  std::vector<float> desired_ball_heights;
  std::vector<int> contact_frames;
  std::vector<int> delays_ms;
  std::vector<float> full_ball_distances;
  std::vector<float> bumpy_ride_biases;
  std::vector<float> impulse_request_speeds;
};
bool &ContactAuthorityAuditEnabled();
bool IsTrackedScheduledContact(e_FunctionType type);
ContactAuthorityAudit &ContactAuthorityFor(e_FunctionType type);
// RecordMovementCommandAcceptance() is declared at the end of this header: it
// needs the action/pose enums defined below.
// Counts one scheduler query and returns whether the candidate differs
// materially from the command already in force.
bool RecordSchedulerQuery(const PlayerCommand &in_force,
                          const PlayerCommand &candidate);

// Called at the single point where a selection replaces the command in force,
// so the recorded reason is the real scheduler decision and not a replay.
void RecordMovementCommandAcceptance(bool material_candidate,
                                     e_FunctionType previous_type,
                                     int previous_elapsed_ms,
                                     e_InterruptAnim interrupt,
                                     const PlayerCommand &command,
                                     int frame_count);


// ANIM-2 A11.0: body-forecast dual-run. The legacy forecast stays the
// production one; the Player's read-only PreviewRawPose is compared against it
// so the authority can be flipped once the two agree bit-exactly.
int &BakedBodyPreviewChecks();


#endif  // FOOTBALL_SIM_PLAYER_PLAYER_DIAGNOSTICS_HPP

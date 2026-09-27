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

#ifndef _HPP_PLAYERBASE
#define _HPP_PLAYERBASE

#include "humanoid/humanoidbase.hpp"
#include "player_kinematics.hpp"
#include "player_ground_collider.hpp"
#include "player_action.hpp"
#include "player_movement_command.hpp"
#include "locomotion_intent_scheduler.hpp"

// H3e4f-g0b-3a: which boundary began the action, so residency can be attributed.
enum ActionBeginReason {
  kBeginMovementToMovement = 0,
  kBeginOtherToMovement,
  kBeginMovementToOther,
  kBeginOtherToOther,
  kBeginResetPosition,
  kBeginResetSituation,
  kBeginRetainSelection,
  kBeginReasonCount,
};

// Caller scope for the remaining ResetSituation raw Movement policy. This is
// observation only; Deactivate's double reset is deliberately not changed here.
enum ResetSituationCallContext {
  kResetSituationUnspecified = 0,
  kResetSituationInitialBeforeFirstPlayerTick,
  kResetSituationRuntime,
  kResetSituationPlayerDeactivateFirst,
  kResetSituationBaseDeactivateSecond,
  kResetSituationCallContextCount
};
int &MaterialResetSituationReanchorsForContext(int context);
const char *ResetSituationCallContextName(int context);
int &SimulationOnlyGateMismatchForResetContext(int context);
struct ReanchorResidency {
  int n = 0;
  std::vector<int> lifetime_ms;
  std::vector<int> ticks;
};
// Source in force immediately before a material raw Movement re-anchor.
// This is the evidence needed to distinguish a lifecycle seed from an
// overwrite of an established simulation-owned intent.
struct MaterialReanchorPreviousSource {
  int action_coupled = 0;
  int direct = 0;
  int legacy_carried = 0;
  int simulation_seed = 0;
  int simulation_fallback = 0;
};
// Consumption of a material lifecycle override before the next successful
// controller-owned DirectMovementIntent publication.
struct LifecycleOverrideConsumption {
  int started = 0;
  int completed_by_direct = 0;
  int interrupted_by_lifecycle = 0;
  int consumed_before_direct = 0;
  std::vector<int> first_consume_delay_ms;
  std::vector<int> locomotion_ticks_before_direct;
  std::vector<int> next_direct_delay_ms;
};
ReanchorResidency &ReanchorResidencyFor(int reason);
// Raw Movement re-anchor occurrences, not residency episodes: an occurrence is
// counted immediately, even if its episode has not closed by corpus end.
int &MaterialMovementReanchorsForBeginReason(int reason);
MaterialReanchorPreviousSource &
MaterialMovementReanchorPreviousSourceForBeginReason(int reason);
LifecycleOverrideConsumption &
LifecycleOverrideConsumptionForBeginReason(int reason);
void DumpReanchorProvenance();
void DumpQueryOpportunities();
const char *ActionBeginReasonName(int reason);

// H3e4f-g0b-prime: is a Movement re-anchor actually consumed by locomotion before the
// next direct refresh? Only that makes it load-bearing.
int &ReanchorPendingSet();
int &ReanchorConsumedBeforeRefresh();
int &ReanchorSupersededByRefresh();
std::vector<int> &ReanchorLifetime_ms();
// H3e4f-g0b-3a provenance: does a restart boundary construct a new locomotion
// command, or carry an older one forward?
int &RestartCarriedCommandForward();
int &RestartConstructedCommand();
int &RetainCarriedCommandForward();
int &RetainConstructedCommand();
// H3e4f-g0b-reset-seed-episode: transient, never serialized. It proves that a
// runtime reset's neutral seed is consumed by locomotion before the next
// controller publication, instead of relying on a golden call index.
struct ResetSeedAuditEpisode {
  bool active = false;
  int context = -1;
  int started_ms = -1;
  int first_consume_ms = -1;
  int direct_publish_ms = -1;
  int locomotion_consumes = 0;
  int foreign_consumes = 0;
  int consumes_before_direct = 0;
};
// Aggregate event counters; the live episode itself is per-player, because one
// reset boundary re-seeds many players before any of them processes a tick.
ResetSeedAuditEpisode &ResetSeedLastClosedEpisode();
int &ResetSeedEpisodesStarted();
int &ResetSeedEpisodesConsumedBeforeDirect();
int &ResetSeedEpisodesDirectFirst();
int &ResetSeedEpisodesRestartedBeforeCompletion();
int &ResetSeedForeignConsumeViolations();
std::vector<int> &ResetSeedConsumeToDirectDelay_ms();
// H3e4f-g0b-decision-intent: the locomotion intent owned by the Player Decision
// Clock. Today it is a pure shadow written only by DirectMovementIntent, so the
// existing compatibility slot keeps driving gameplay. It exists to answer one
// question first: does a decision-owned intent exist on the ticks that actually
// execute procedural locomotion, or is the slot only holding action-coupled
// compatibility payload?
struct PlayerDecisionLocomotionState {
  PlayerCommand command;
  bool initialized = false;
  // H3e4f-g0b-continuity-epoch: validity is a continuity property, not a time
  // one. The epoch advances when the world leaves locomotion continuity, so an
  // intent published in an earlier epoch must not drive the new one. These two
  // epochs decide whether a continuity repair queries the controller, so they
  // are gameplay state and are serialized.
  unsigned long long continuityEpoch = 0;
  unsigned long long publishedEpoch = 0;
  // Continuity tracking. Gameplay, not audit: leaving eligibility advances the
  // epoch.
  bool wasEligibleLastTick = false;
  bool continuityStarted = false;
  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    command.ProcessState(state);
    state->process(initialized);
    state->process(continuityEpoch);
    state->process(publishedEpoch);
    state->process(wasEligibleLastTick);
    state->process(continuityStarted);
  }
};
int &DecisionLocomotionIntentPresentTicks();
int &DecisionLocomotionIntentMissingTicks();
int &DecisionLocomotionIntentMissingForSource(int source);
long &DecisionLocomotionIntentAgeSum_ms();
int &DecisionLocomotionIntentAgeCount();
int &DecisionLocomotionIntentAgeMax_ms();
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
  int legacy_gate_true = 0;
  int legacy_gate_false = 0;
  int scheduler_due = 0;
  int scheduler_not_due = 0;
  int unchanged_and_legacy_gate_false = 0;
  long decision_age_sum_ms = 0;
  int decision_age_count = 0;
  int decision_age_max_ms = -1;
  // Shadow of the epoch rule: this epoch has no publication yet, so a decision
  // would be required before this locomotion state could be executed.
  int would_require_fresh = 0;
  // Attribution of a stale epoch: if the last reset is newer than the last
  // publication, the reset advanced the epoch and no decision has caught up.
  int stale_after_reset = 0;
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
#include "../../data/playerdata.hpp"
#include "controller/icontroller.hpp"
#include "../../onthepitch/humangamer.hpp"


class Match;
class HumanController;
class HumanoidBase;

class PlayerBase {

  friend class HumanoidBase;

  public:
    PlayerBase(Match *match, PlayerData *playerData);
    virtual ~PlayerBase();
    void Mirror();

    inline int GetStableID() const { return stable_id; }
    inline const PlayerData* GetPlayerData() { DO_VALIDATION; return playerData; }

    inline bool IsActive() { DO_VALIDATION; return isActive; }

    // get ready for some action
    virtual void Activate(boost::shared_ptr<AnimCollection> animCollection, bool lazyPlayer) = 0;
    // go back to bench/take a shower
    virtual void Deactivate();

    void ResetPosition(const Vector3 &newPos, const Vector3 &focusPos);
    void OffsetPosition(const Vector3 &offset);

    bool HasDecisionLocomotionIntent() const {
      return decisionLocomotionState.initialized;
    }
    // H3e4f-g0b-reentry-audit: which locomotion entry this tick is, and whether
    // the decision intent behind it was refreshed since locomotion was left.
    bool IsLocomotionIntentRefreshDue(int now_ms) const {
      return locomotionIntentScheduler.Due(now_ms);
    }
    void NoteLocomotionReentryTick(bool eligible, bool legacy_gate,
                                   bool scheduler_due, int now_ms);
    // Gameplay continuity transition: advances the epoch when the actor stops
    // being eligible for procedural locomotion. Called once per player tick.
    void AdvanceLocomotionContinuity(bool eligible);
    // step 2 trigger: this epoch's locomotion state has no decision publication,
    // so an older intent must not drive execution before a fresh decision.
    bool DecisionLocomotionEpochIsStale() const {
      return decisionLocomotionState.publishedEpoch !=
             decisionLocomotionState.continuityEpoch;
    }
    // Cause of the next Direct publication (0 cadence, 1 legacy only, 2 repair).
    void NoteDecisionPublicationCause(int cause);
    inline int GetFrameNum() { DO_VALIDATION; return humanoid->GetFrameNum(); }
    inline int GetFrameCount() { DO_VALIDATION; return humanoid->GetFrameCount(); }

    // Gameplay-facing movement state. It is NOT yet the producer: Humanoid's
    // spatial state is still authoritative for movement, and `kinematicState`
    // is an exact mirror of it. H3e flips this producer relationship. Every
    // mutation of the Humanoid spatial state refreshes the mirror at the
    // mutation point (CalculateSpatialState, CalculateFactualSpatialState,
    // OffsetPosition, Mirror, state restore), so mid-tick readers such as the
    // controller never see a stale actor. Mirroring follows the legacy
    // field-level asymmetry (position/velocity mirrored; facing/bodyFacing not).
    // bodyFacing is simulation-authoritative for pure locomotion (H3e3b) and
    // remains an exact legacy shadow for every other action. Its public reader
    // therefore uses the kinematic state; Humanoid is only its compatibility
    // projection/oracle at this boundary.
    // CheckSimulationKinematicOracle() enforces the mirror bit-exactly.
    // Actors must be positioned through PlayerBase::ResetPosition /
    // OffsetPosition so that this state cannot be left stale.
    inline Vector3 GetPosition() const { return kinematicState.position; }
    inline Vector3 GetDirectionVec() const { return kinematicState.facing; }
    inline Vector3 GetBodyDirectionVec() const { return kinematicState.bodyFacing; }
    inline Vector3 GetMovement() const { return kinematicState.velocity; }
    // Gameplay reads continuous relative orientation. The quantized Humanoid
    // relBodyAngle remains animation-selection compatibility only.
    inline radian GetRelBodyAngle() const {
      return humanoid->GetRelBodyAngleNonquantized();
    }
    inline e_Velocity GetEnumVelocity() const { return humanoid->GetEnumVelocity(); }
    inline float GetFloatVelocity() const {
      return EnumToFloatVelocity(humanoid->GetEnumVelocity());
    }
    inline e_FunctionType GetCurrentFunctionType() const {
      return GetSimulationActionState().type;
    }
    inline e_FunctionType GetPreviousFunctionType() const { return humanoid->GetPreviousFunctionType(); }
    // H3e authority boundary: true only when this actor may leave the legacy
    // animation root-motion path and use the simulation-owned procedural
    // movement model instead. See PlayerActionState::IsPureLocomotion().
    bool IsEligibleForProceduralLocomotion() const;
    // Simulation-owned execution gate for Movement locomotion. This deliberately
    // does not consult Humanoid::currentAnim.originatingCommand.
    bool HasSimulationLocomotionIntent() const;
    LocomotionCommandSource GetSimulationMovementCommandSource() const {
      return movementCommandState.source;
    }
    bool IsSimulationMovementCommandInitialized() const {
      return movementCommandState.initialized;
    }
    int GetLastDirectMovementIntentPublication_ms() const {
      return lastDirectMovementIntentPublication_ms;
    }
    int GetLastResetSituation_ms() const { return lastResetSituation_ms; }
    int GetLastResetSituationAuditContext() const {
      return lastResetSituationAuditContext;
    }
    void NoteProcessedPlayerTick() { hasProcessedPlayerTick = true; }
    const PlayerKinematicState &GetKinematicState() const {
      DO_VALIDATION;
      return kinematicState;
    }
    const PlayerKinematicState &GetKinematicShadow() const {
      DO_VALIDATION;
      return kinematicShadow;
    }
    const PlayerGroundCollider &GetGroundCollider() const {
      DO_VALIDATION;
      return groundCollider;
    }
    // Gameplay reads the sole persistent, simulation-authoritative action
    // schedule. Humanoid provides only a temporary legacy oracle.
    const PlayerActionState &GetSimulationActionState() const {
      DO_VALIDATION;
      return actionState;
    }
    // H3e4f-a: the Movement command procedural locomotion consumes. The legacy
    // animation scheduler still produces it; storage already belongs here.
    const PlayerCommand &GetSimulationMovementCommand() const {
      DO_VALIDATION;
      return movementCommandState.command;
    }
    void SetSimulationMovementCommand(const PlayerCommand &command);
    void SetSimulationMovementCommand(const PlayerCommand &command,
                                      LocomotionCommandSource source);
    // Fatal invariant: on every pure-locomotion tick the shadow and legacy
    // currentAnim.originatingCommand must agree on every field locomotion reads.
    void CheckSimulationMovementCommandOracle() const;
    // H3e4f-c2a: observation only. The simulation keeps its own locomotion
    // intent cadence in parallel with the animation requeue lifecycle, so the
    // two schedules can be compared before either one takes over.
    void ObserveLocomotionIntentCadence(bool legacy_opportunity);
    // c2b-2: returns whether the simulation must refresh the intent now. A due
    // tick during a non-locomotion action is held overdue rather than consumed,
    // so the refresh happens as soon as the actor is eligible again.
    bool NoteLocomotionIntentCadence(bool legacy_opportunity);
    // Called only once the controller was actually queried for this refresh.
    void CommitLocomotionIntentRefresh();
    void CloseReanchorEpisode();
    void NoteControllerQuery(bool had_movement_candidate);
    bool PublishMovementIntentFromQueue(const PlayerCommandQueue &queue);
    void NoteMaterialMovementReanchor();
    bool LocomotionIntentRefreshHeldIneligible() const;
    // Called where locomotion actually reads the command.
    void NoteLocomotionCommandConsumed();
    // H3d2a: Humanoid invokes these for completed action selection and ticks.
    // The executor advances independently; legacy state is an oracle only.
    void BeginSimulationAction();
    void StepSimulationAction(int elapsedTime_ms);
    // Validates the authoritative schedule against Humanoid's temporary
    // legacy projection. It is intentionally a fatal invariant in all builds.
    void CheckSimulationActionOracle() const;

    void TripMe(const Vector3 &tripVector, int tripType) { DO_VALIDATION; humanoid->TripMe(tripVector, tripType); }

    void RequestCommand(PlayerCommandQueue &commandQueue);
    IController *GetController();
    void SetExternalController(HumanGamer *externalController);
    HumanController *ExternalController();
    bool ExternalControllerActive();

    // Bit-exact mirror check: position, velocity, locomotion facing, body
    // facing, derived speed and the collider center must all match the Humanoid
    // spatial state exactly. An epsilon cannot mask a synchronization bug.
    bool IsKinematicMirrorConsistent() const;
    // Fatal (all build types) form of the same invariant, with the diverging
    // field reported. Callers are the mirror writers and state restore, so the
    // contract is checked at every movement mutation point.
    void CheckSimulationKinematicOracle() const;

    float GetDecayingPositionOffsetLength() { DO_VALIDATION; return humanoid->GetDecayingPositionOffsetLength(); }

    virtual void Process();


    virtual float GetStat(PlayerStat name) const;
    float GetVelocityMultiplier() const;
    float GetMaxVelocity() const;

    const Anim *GetCurrentAnim() { DO_VALIDATION; return humanoid->GetCurrentAnim(); }

    void SetLastTouchTime_ms(unsigned long touchTime_ms) { DO_VALIDATION; this->lastTouchTime_ms = touchTime_ms; }
    unsigned long GetLastTouchTime_ms() { DO_VALIDATION; return lastTouchTime_ms; }
    void SetLastTouchType(e_TouchType touchType) { DO_VALIDATION; this->lastTouchType = touchType; }
    e_TouchType GetLastTouchType() { DO_VALIDATION; return lastTouchType; }
    float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0);


    float GetFatigueFactorInv() const { return fatigueFactorInv; }
    void RelaxFatigue(float howMuch) { DO_VALIDATION;
      fatigueFactorInv += howMuch;
      fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
    }

    virtual void ResetSituation(const Vector3 &focusPos);

    void ProcessStateBase(EnvState* state);

  protected:
    void SynchronizeKinematicState();
    void UpdateKinematicShadow();
    void ResetKinematicShadow();
    PlayerActionState CaptureLegacyActionState() const;
    void SetNextResetSituationAuditContext(int context);
    void BeginLifecycleOverrideConsumptionAudit(int reason);
    void NoteLifecycleOverrideConsumptionAudit();
    void CompleteLifecycleOverrideConsumptionAudit();
    Match *match;

    const PlayerData* const playerData;
    const int stable_id = 0;

    std::unique_ptr<HumanoidBase> humanoid;
    PlayerKinematicState kinematicState;
    PlayerKinematicState kinematicShadow;
    PlayerGroundCollider groundCollider;
    PlayerActionState actionState;
    PlayerMovementCommandState movementCommandState;
    LocomotionIntentScheduler locomotionIntentScheduler;
    bool locomotionIntentDueThisTick = false;
    bool reanchorPendingConsumption = false;
    bool lifecycleOverrideAuditActive = false;
    int lifecycleOverrideAuditReason = kBeginMovementToMovement;
    int lifecycleOverrideAuditStart_ms = 0;
    int lifecycleOverrideAuditTicks = 0;
    int lifecycleOverrideAuditFirstConsumeDelay_ms = -1;
    // Explicit cause of the next Direct publication. A continuity repair runs
    // before NoteLocomotionIntentCadence, so deriving the cause from that flag
    // would mislabel a repair publication as an animation-driven one.
    int pendingPublicationCause = 0;
    // Audit generation: bumped on every Direct publication for re-entry freshness
    // measurement. Deliberately transient so telemetry cannot change the save-state
    // contract; the gameplay epochs above are serialized instead.
    unsigned long long decisionLocomotionAuditGeneration = 0;
    int lastDirectMovementIntentPublication_ms = -1;
    int lastResetSituation_ms = -1;
    bool hasProcessedPlayerTick = false;
    // Transient per-player reset-seed episode. Never serialized and never part
    // of gameplay: it only proves this player's seed was consumed before the
    // controller replaced it.
    ResetSeedAuditEpisode resetSeedAudit;
    int lastResetSituationAuditContext =
        kResetSituationInitialBeforeFirstPlayerTick;
    int tr_query_gen = 0;
    // Decision-owned locomotion intent shadow. Measurement only for now: it is
    // written only by a DirectMovementIntent publication.
    PlayerDecisionLocomotionState decisionLocomotionState;
    // Re-entry provenance. Transient, never serialized, measurement only.
    int reentryAuditEpoch = -1;
    bool locomotionExitRecorded = false;
    bool resetGenerationAnchorValid = false;
    unsigned long long resetDecisionGeneration = 0;
    bool reentryAuditStarted = false;
    bool wasPureLocomotionLastTick = false;
    bool resetSinceLastPlayerTick = false;
    // Cause and eligibility of the latest Direct publication, and that of the
    // publication that made a re-entry generation fresh.
    bool lastPublicationViaSimulationCadence = false;
    bool lastPublicationWhileIneligible = false;
    unsigned long long decisionGenerationAtLocomotionExit = 0;
    int GetDecisionLocomotionIntentGeneration() const {
      return static_cast<int>(decisionLocomotionAuditGeneration);
    }
    int tr_last_query_ms = -1;
    int tr_last_query_due = 0;
    int tr_last_query_eligible = 0;
    int tr_last_query_action = 0;
    int tr_last_query_retains = 0;
    int tr_last_query_had_candidate = 0;
    int reanchorPendingSince_ms = -1;
    int resetSituationAuditContext = kResetSituationUnspecified;
    int nextActionBeginReason = kBeginMovementToMovement;
    bool episode_active = false;
    int episode_active_reason = 0;
    int episode_start_ms = 0;
    int episode_ticks = 0;
    std::unique_ptr<IController> controller;
    HumanGamer *externalController = 0;

    bool isActive = false;

    unsigned long lastTouchTime_ms = 0;
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    std::vector<Vector3> positionHistoryPerSecond; // resets too (on ResetSituation() calls)

};

#endif

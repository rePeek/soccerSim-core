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
#include "core/physics/player_movement.hpp"
#include "core/domain/player/player.hpp"
#include "player_ground_collider.hpp"
#include "player_action.hpp"
#include "locomotion_intent_scheduler.hpp"
#include "player_decision_scheduler.hpp"

// Caller scope for the remaining ResetSituation instrumentation. This is
// observation only; Deactivate's double reset is deliberately not changed here.
enum ResetSituationCallContext {
  kResetSituationUnspecified = 0,
  kResetSituationInitialBeforeFirstPlayerTick,
  kResetSituationRuntime,
  kResetSituationPlayerDeactivateFirst,
  kResetSituationBaseDeactivateSecond,
  kResetSituationCallContextCount
};
const char *ResetSituationCallContextName(int context);
int &SimulationOnlyGateMismatchForResetContext(int context);
void DumpQueryOpportunities();

// 4f-a2 diagnostic shadow, retained for queue-equivalence telemetry. 4f-a3a
// introduces the separate serialized PlayerDecisionQueueState below; this shadow
// is never consumed by gameplay and remains transient.
struct SimulationDecisionQueueShadow {
  PlayerCommandQueue commands;
  bool initialized = false;
  unsigned long long generation = 0;
  int updated_ms = -1;
};

// 4f-a3a: serialized gameplay authority for the complete controller decision.
struct PlayerDecisionQueueState {
  PlayerCommandQueue commands;
  bool initialized = false;
  unsigned long long generation = 0;
  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    int size = static_cast<int>(commands.size());
    state->process(size);
    if (state->Load()) commands.resize(size);
    for (PlayerCommand &command : commands) command.ProcessState(state);
    state->process(initialized);
    state->process(generation);
  }
};
// H3e4f-g0b-decision-intent: the serialized Player Decision Clock locomotion
// state. Since the execution authority flip this is the sole command source for
// pure-locomotion execution: the gate requires a current-epoch publication, and
// both PlayerLocomotion and PlayerBodyFacing consume this command.
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
  int scheduler_due = 0;
  int scheduler_not_due = 0;
  long decision_age_sum_ms = 0;
  int decision_age_count = 0;
  int decision_age_max_ms = -1;
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
#include "../../data/playerdata.hpp"
#include "controller/icontroller.hpp"
#include "../../onthepitch/humangamer.hpp"


class Match;
class HumanController;
class HumanoidBase;

class PlayerBase {

  friend class HumanoidBase;

  public:
    PlayerBase(Match *match, PlayerData *playerData,
               PlayerState *world_player_state = nullptr,
               const football::domain::PlayerProfile *world_profile = nullptr);
    virtual ~PlayerBase();
    void Mirror();

    inline int GetStableID() const { return stable_id; }
    inline const PlayerData* GetPlayerData() { DO_VALIDATION; return playerData; }

    inline bool IsActive() { DO_VALIDATION; return isActive; }

    // get ready for some action
    virtual void Activate(std::shared_ptr<AnimCollection> animCollection, bool lazyPlayer) = 0;
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
    void NoteLocomotionReentryTick(bool eligible, bool scheduler_due,
                                   int now_ms);
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
    // Read-only view of the serialized decision continuity state. A restore must
    // reproduce the repair decision, so these are gameplay invariants and must
    // not be compared against the transient audit generation.
    unsigned long long GetDecisionLocomotionContinuityEpoch() const {
      return decisionLocomotionState.continuityEpoch;
    }
    unsigned long long GetDecisionLocomotionPublishedEpoch() const {
      return decisionLocomotionState.publishedEpoch;
    }
    bool DecisionLocomotionContinuityStarted() const {
      return decisionLocomotionState.continuityStarted;
    }
    bool WasDecisionLocomotionEligibleLastTick() const {
      return decisionLocomotionState.wasEligibleLastTick;
    }
    // H3e4f-g0b-authority-flip: the executable locomotion intent is the one the
    // Player Decision Clock published in the current continuity epoch. This is
    // what execution authority will read, instead of the animation command.
    bool HasExecutableDecisionLocomotionIntent() const {
      return decisionLocomotionState.initialized &&
             decisionLocomotionState.command.useDesiredMovement &&
             decisionLocomotionState.publishedEpoch ==
                 decisionLocomotionState.continuityEpoch;
    }
    const PlayerCommand &GetDecisionLocomotionIntent() const {
      DO_VALIDATION;
      return decisionLocomotionState.command;
    }
    // Decision-side execution oracle. The gate answers executability; this answers
    // the producer contract, which stays fatal rather than being folded into the
    // gate predicate.
    void CheckDecisionLocomotionIntentOracle() const;
    inline int GetFrameNum() { DO_VALIDATION; return humanoid->GetFrameNum(); }
    inline int GetFrameCount() { DO_VALIDATION; return humanoid->GetFrameCount(); }

    // Gameplay-facing authoritative self-movement state. Normal ticks and
    // reset/lifecycle discontinuities produce PlayerKinematicResult, with
    // SpatialState checked as a bit-exact compatibility projection.
    // Collision-driven OffsetPosition is an interaction correction: its legacy
    // reverse sync is intentionally deferred to 7G contact resolution.
    //
    // Mirroring preserves the legacy field asymmetry (position/velocity mirror;
    // movementFacing/torsoFacing do not). The compatibility copy and oracle
    // remain until a separate save-format migration.
    inline Vector3 GetPosition() const { return kinematicState.position; }
    inline Vector3 GetDirectionVec() const { return kinematicState.movementFacing; }
    inline Vector3 GetBodyDirectionVec() const { return kinematicState.torsoFacing; }
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
    const football::domain::Player& Entity() const { return domainPlayer; }
    // Sole mutation point for evaluated movement kinematics. Callers must
    // project this state to HumanoidBase::SpatialState before exposing the tick.
    void ApplyKinematicResult(const PlayerKinematicResult &result);
    const PlayerGroundCollider &GetGroundCollider() const {
      DO_VALIDATION;
      return groundCollider;
    }
    // Canonical collider for new simulation/contact consumers: computed from
    // the authoritative profile and state. GetGroundCollider() above is only a
    // serialized compatibility shadow kept bit-equal by CheckGroundColliderOracle().
    PlayerGroundCollider GetDerivedGroundCollider() const;
    // Gameplay reads the sole persistent, simulation-authoritative action
    // schedule. Humanoid provides only a temporary legacy oracle.
    const PlayerActionState &GetSimulationActionState() const {
      DO_VALIDATION;
      return actionState;
    }
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
    void NoteControllerQuery(bool had_movement_candidate);
    // c2a: the Player Decision Clock's only publication entry point. It owns the
    // decision locomotion state and the publication telemetry, and never touches the
    // compatibility movement command slot.
    void PublishDecisionLocomotionIntent(const PlayerCommand &command);
    // 4f-a1: whether the animation selection on this tick picked a Movement clip,
    // so a legacy-only publication can be cross-tabulated with the selection.
    void NoteDecisionMovementSelection(bool movement_selected);
    // 4f-a2: update the shadow from a simulation-owned controller query. A
    // legacy-only query must never call this: the shadow answers what the last
    // decision would be if animation-owned queries had never existed.
    void ObserveSimulationDecisionQueue(const PlayerCommandQueue &commands,
                                        int now_ms);
    bool IsPlayerDecisionRefreshDue(int now_ms, int cadence_ms) const {
      return playerDecisionScheduler.Due(now_ms, cadence_ms);
    }
    void PublishPlayerDecisionQueue(const PlayerCommandQueue &commands,
                                    int now_ms);
    bool HasSimulationDecisionQueue() const {
      return simulationDecisionQueue.initialized;
    }
    const PlayerCommandQueue &GetSimulationDecisionQueue() const {
      DO_VALIDATION;
      return simulationDecisionQueue.commands;
    }
    int GetSimulationDecisionQueueAge_ms(int now_ms) const {
      return simulationDecisionQueue.updated_ms < 0
          ? -1
          : now_ms - simulationDecisionQueue.updated_ms;
    }
    // 4f-a3a: independent Player Decision Clock and its serialized latest queue.
    bool HasPlayerDecisionQueue() const { return playerDecisionQueue.initialized; }
    const PlayerCommandQueue &GetPlayerDecisionQueue() const {
      DO_VALIDATION;
      return playerDecisionQueue.commands;
    }
    unsigned long long GetPlayerDecisionGeneration() const {
      return playerDecisionQueue.generation;
    }
    bool PublishMovementIntentFromQueue(const PlayerCommandQueue &queue);
    bool LocomotionIntentRefreshHeldIneligible() const;
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
    // Same invariant for the legacy collider shadow: center and radius must be
    // exactly the Derived collider of the authoritative profile and state.
    void CheckGroundColliderOracle() const;
    // Writes the compatibility shadow from the authoritative inputs. This is a
    // projection, not a gameplay mutation; the derived collider is canonical.
    void ProjectGroundColliderShadow();

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
    PlayerActionState CaptureLegacyActionState() const;
    void SetNextResetSituationAuditContext(int context);
    Match *match;

    const PlayerData* const playerData;
    const int stable_id = 0;

    std::unique_ptr<HumanoidBase> humanoid;
    PlayerKinematicState localKinematicState;  // officials (not team slots)
    football::domain::PlayerProfile localProfile;  // bench / officials
    football::domain::Player domainPlayer;
    PlayerKinematicState &kinematicState;  // aliases domainPlayer.State()
    PlayerGroundCollider groundCollider;  // compatibility/serialized shadow
    PlayerActionState actionState;
    LocomotionIntentScheduler locomotionIntentScheduler;
    bool locomotionIntentDueThisTick = false;
    // Explicit cause of the next Direct publication. A continuity repair runs
    // before NoteLocomotionIntentCadence, so deriving the cause from that flag
    // would mislabel a repair publication as an animation-driven one.
    int pendingPublicationCause = 0;
    // 4f-a1: transient, per-tick animation selection outcome. Never serialized.
    bool decisionMovementSelection = false;
    // 4f-a2: transient simulation-owned decision queue shadow. Not serialized.
    SimulationDecisionQueueShadow simulationDecisionQueue;
    PlayerDecisionScheduler playerDecisionScheduler;
    PlayerDecisionQueueState playerDecisionQueue;
    // Audit generation: bumped on every Direct publication for re-entry freshness
    // measurement. Deliberately transient so telemetry cannot change the save-state
    // contract; the gameplay epochs above are serialized instead.
    unsigned long long decisionLocomotionAuditGeneration = 0;
    int lastDirectMovementIntentPublication_ms = -1;
    int lastResetSituation_ms = -1;
    bool hasProcessedPlayerTick = false;
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
    int resetSituationAuditContext = kResetSituationUnspecified;
    std::unique_ptr<IController> controller;
    HumanGamer *externalController = 0;

    bool isActive = false;

    unsigned long lastTouchTime_ms = 0;
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    std::vector<Vector3> positionHistoryPerSecond; // resets too (on ResetSituation() calls)

};

#endif

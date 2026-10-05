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

#ifndef _HPP_PLAYER
#define _HPP_PLAYER

#include <cstdint>
#include <memory>
#include <optional>
#include "model/player.hpp"
#include "sim/player/humanoid/humanoid.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_ground_collider.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/locomotion_intent_scheduler.hpp"
#include "sim/player/player_decision_scheduler.hpp"
#include "control/player_control.hpp"

// Caller scope for the remaining ResetSituation instrumentation. This is
// observation only; Deactivate's double reset is deliberately not changed here.
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
#include "model/player.hpp"
#include "sim/player/controller/icontroller.hpp"
#include "sim/humangamer.hpp"


class Match;
class HumanController;
class HumanoidBase;

struct TacticalPlayerSituation {
  float forwardSpaceRating = 0.0f;
  float toGoalSpaceRating = 0.0f;
  float spaceRating = 0.0f;
  float forwardRating = 0.0f;
};

class Team;
class ElizaController;

// Concrete football-player runtime; no shared official-actor base remains.
class Player final {

  friend class HumanoidBase;

  public:
    Player(Team *team, const football::model::Player& model, std::uint8_t schedule_phase);
    ~Player();
    void Mirror();

    football::model::PlayerId GetID() const { return model_.id; }
    const football::model::Player& GetModel() const { return model_; }

    inline bool IsActive() { return isActive; }

    // get ready for some action
    void Activate(bool lazyPlayer);
    // go back to bench/take a shower
    void Deactivate();

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
      return decisionLocomotionState.command;
    }
    // Decision-side execution oracle. The gate answers executability; this answers
    // the producer contract, which stays fatal rather than being folded into the
    // gate predicate.
    void CheckDecisionLocomotionIntentOracle() const;
    inline int GetFrameNum() { return humanoid->GetFrameNum(); }
    inline int GetFrameCount() { return humanoid->GetFrameCount(); }

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
    // Actors must be positioned through Player::ResetPosition /
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
    int GetLastDirectMovementIntentPublication_ms() const {
      return lastDirectMovementIntentPublication_ms;
    }
    int GetLastResetSituation_ms() const { return lastResetSituation_ms; }
    int GetLastResetSituationAuditContext() const {
      return lastResetSituationAuditContext;
    }
    void NoteProcessedPlayerTick() { hasProcessedPlayerTick = true; }
    const PlayerKinematicState &GetKinematicState() const {
      return kinematicState;
    }
    const PlayerGroundCollider &GetGroundCollider() const {
      return groundCollider;
    }
    // Gameplay reads the sole persistent, simulation-authoritative action
    // schedule. Humanoid provides only a temporary legacy oracle.
    const PlayerActionState &GetSimulationActionState() const {
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

    void TripMe(const Vector3 &tripVector, int tripType) { humanoid->TripMe(tripVector, tripType); }

    void RequestCommand(PlayerCommandQueue &commandQueue);
    IController *GetController();
    void SetExternalController(HumanGamer *externalController);
    HumanController *ExternalController();
    bool ExternalControllerActive();
    void SetControl(const PlayerControl& control) { control_ = control; }
    void ClearControl() { control_.reset(); }

    // Bit-exact mirror check: position, velocity, locomotion facing, body
    // facing, derived speed and the collider center must all match the Humanoid
    // spatial state exactly. An epsilon cannot mask a synchronization bug.
    bool IsKinematicMirrorConsistent() const;
    // Fatal (all build types) form of the same invariant, with the diverging
    // field reported. Callers are the mirror writers and state restore, so the
    // contract is checked at every movement mutation point.
    void CheckSimulationKinematicOracle() const;

    float GetDecayingPositionOffsetLength() { return humanoid->GetDecayingPositionOffsetLength(); }

    void Process();


    float GetStat(football::model::PlayerStat name) const;
    float GetVelocityMultiplier() const;
    float GetMaxVelocity() const;

    const Anim *GetCurrentAnim() { return humanoid->GetCurrentAnim(); }
    Match *GetMatch() { return match; }

    void SetLastTouchTime_ms(unsigned long touchTime_ms) { this->lastTouchTime_ms = touchTime_ms; }
    unsigned long GetLastTouchTime_ms() { return lastTouchTime_ms; }
    void SetLastTouchType(e_TouchType touchType) { this->lastTouchType = touchType; }
    e_TouchType GetLastTouchType() { return lastTouchType; }
    float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0);


    float GetFatigueFactorInv() const { return fatigueFactorInv; }
    void RelaxFatigue(float howMuch) {
      fatigueFactorInv += howMuch;
      fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
    }

    void ResetSituation(const Vector3 &focusPos);

    Humanoid *CastHumanoid();
    ElizaController *CastController();
    int GetTeamID() const;
    Team *GetTeam();
    Vector3 GetPitchPosition();
    bool TouchPending() const { return GetSimulationActionState().IsContactPending(); }
    bool TouchAnim() const { return GetSimulationActionState().HasScheduledContact(); }
    Vector3 GetTouchPos() const { return GetSimulationActionState().contactPosition; }
    int GetTouchFrame() const { return GetSimulationActionState().contactFrame; }
    int GetCurrentFrame() const { return GetSimulationActionState().frame; }
    void SelectRetainAnim() { humanoid->SelectRetainAnim(); }
    FormationEntry GetFormationEntry();
    void SetDynamicFormationEntry(FormationEntry entry) { dynamicFormationEntry = entry; }
    FormationEntry GetDynamicFormationEntry() { return dynamicFormationEntry; }
    void SetManMarking(Player* player) { manMarking = player; }
    Player* GetManMarking() { return manMarking; }
    bool HasPossession() const;
    bool HasBestPossession() const;
    bool HasUniquePossession() const;
    int GetPossessionDuration_ms() const { return possessionDuration_ms; }
    int GetTimeNeededToGetToBall_ms() const { return timeNeededToGetToBall_ms; }
    int GetTimeNeededToGetToBall_optimistic_ms() const { return timeNeededToGetToBall_optimistic_ms; }
    int GetTimeNeededToGetToBall_previous_ms() const { return timeNeededToGetToBall_previous_ms; }
    void SetDesiredTimeToBall_ms(int ms) { desiredTimeToBall_ms = ms; }
    int GetDesiredTimeToBall_ms() const { return clamp(desiredTimeToBall_ms, timeNeededToGetToBall_ms, 1000000.0f); }
    bool AllowLastDitch(bool includingPossessionAmount = true) const;
    void TriggerControlledBallCollision() { triggerControlledBallCollision = true; }
    bool IsControlledBallCollisionTriggered() { return triggerControlledBallCollision; }
    void ResetControlledBallCollisionTrigger() { triggerControlledBallCollision = false; }
    float GetAverageVelocity(float timePeriod_sec);
    void UpdatePossessionStats();
    float GetClosestOpponentDistance() const;
    const TacticalPlayerSituation &GetTacticalSituation() { return tacticalSituation; }
    void Put2D(bool mirror);
    void Hide2D();
    void GiveYellowCard(unsigned long giveTime_ms) { cards++; cardEffectiveTime_ms = giveTime_ms; }
    void GiveRedCard(unsigned long giveTime_ms) { cards += 3; cardEffectiveTime_ms = giveTime_ms; }
    bool HasCards() { return cards > 0; }
    void SendOff();
    float GetStaminaStat() const;

  private:
    void ResetRuntimeState(const Vector3 &focusPos);
    void _CalculateTacticalSituation();
    void SynchronizeKinematicState();
    PlayerActionState CaptureLegacyActionState() const;
    void SetNextResetSituationAuditContext(int context);
    Match *match;

    // Team owns an immutable description for the entire actor lifetime.
    const football::model::Player& model_;
    // Non-unique 0..9 offset for staggered 100 ms work, never identity or order.
    const std::uint8_t schedule_phase_;

    std::unique_ptr<Humanoid> humanoid;
    PlayerKinematicState kinematicState;
    PlayerGroundCollider groundCollider;
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
    std::optional<PlayerControl> control_;

    bool isActive = false;

    unsigned long lastTouchTime_ms = 0;
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    std::vector<Vector3> positionHistoryPerSecond; // resets too (on ResetSituation() calls)

    Team *team = nullptr;
    Player* manMarking = 0;
    FormationEntry dynamicFormationEntry;
    bool hasPossession = false;
    bool hasBestPossession = false;
    bool hasUniquePossession = false;
    int possessionDuration_ms = 0;
    unsigned int timeNeededToGetToBall_ms = 1000;
    unsigned int timeNeededToGetToBall_optimistic_ms = 1000;
    unsigned int timeNeededToGetToBall_previous_ms = 1000;
    bool triggerControlledBallCollision = false;
    TacticalPlayerSituation tacticalSituation;
    int desiredTimeToBall_ms = 0;
    int cards = 0; // 1: yellow; 2: second yellow; >=3: red
    unsigned long cardEffectiveTime_ms = 0;

};

#endif

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
#include <span>
#include "model/player.hpp"
#include "model/pitch.hpp"
#include "sim/player/humanoid/humanoid.hpp"
#include "sim/player/player_kinematics.hpp"
#include "sim/player/player_ground_collider.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/locomotion_intent_scheduler.hpp"
#include "sim/player/player_decision_scheduler.hpp"
#include "sim/player/player_control.hpp"
#include "sim/team/formation_entry.hpp"
#include "sim/event/touch_type.hpp"
#include "foundation/time/tick_boundary.hpp"
#include "sim/player/player_tick_context.hpp"

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

class MentalImage;
namespace football::sim { class BallTouchSink; class PlayerRuntimeSink; }
class HumanoidBase;
class AnimationLibrary;
struct PlayerCommandInputs;

struct TacticalPlayerSituation {
  float forwardSpaceRating = 0.0f;
  float toGoalSpaceRating = 0.0f;
  float spaceRating = 0.0f;
  float forwardRating = 0.0f;
};

class Team;

// Concrete football-player runtime; no shared official-actor base remains.
class Player final {

  friend class HumanoidBase;

  public:
    Player(Team *team, const football::model::Player& model, std::uint8_t schedule_phase,
           const AnimationLibrary& animations, const football::model::Pitch& pitch, blunted::Rng& rng);
    ~Player();
    // Explicit teardown reset before deletion; no hidden clock/RNG destructor path.
    void Exit(football::sim::Tick now);
    void Mirror();

    football::model::PlayerId GetID() const { return model_.id; }
    const football::model::Player& GetModel() const { return model_; }

    inline bool IsActive() { return isActive; }

    // get ready for some action
    void Activate();
    // go back to bench/take a shower
    void Deactivate(const football::ball::Ball& ball, football::sim::Tick now);

    void ResetPosition(const Vector3 &newPos, const Vector3 &focusPos);
    void OffsetPosition(const Vector3 &offset);

    bool HasDecisionLocomotionIntent() const {
      return decisionLocomotionState.initialized;
    }
    // H3e4f-g0b-reentry-audit: which locomotion entry this tick is, and whether
    // the decision intent behind it was refreshed since locomotion was left.
    bool IsLocomotionIntentRefreshDue(football::sim::Tick now) const {
      return locomotionIntentScheduler.Due(now);
    }
    void NoteLocomotionReentryTick(bool eligible, bool scheduler_due,
                                   football::sim::Tick now);
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
    bool IsEligibleForProceduralLocomotion(bool retaining_ball) const;
    std::optional<football::sim::Tick> GetLastDecisionLocomotionPublicationTick() const {
      return last_locomotion_publication_tick_;
    }
    std::optional<football::sim::Tick> GetLastResetSituationTick() const {
      return last_reset_tick_;
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
    // c2b-2: returns whether the simulation must refresh the intent now. A due
    // tick during a non-locomotion action is held overdue rather than consumed,
    // so the refresh happens as soon as the actor is eligible again.
    bool NoteLocomotionIntentCadence(bool legacy_opportunity, football::sim::Tick now, bool retaining_ball);
    // Called only once the controller was actually queried for this refresh.
    void CommitLocomotionIntentRefresh(const football::ball::Ball& ball, football::sim::Tick now, bool retaining_ball);
    void NoteControllerQuery(bool had_movement_candidate, football::sim::Tick now, bool retaining_ball);
    // c2a: the Player Decision Clock's only publication entry point. It owns the
    // decision locomotion state and the publication telemetry, and never touches the
    // compatibility movement command slot.
    void PublishDecisionLocomotionIntent(const PlayerCommand &command, football::sim::Tick now, bool retaining_ball);
    // 4f-a1: whether the animation selection on this tick picked a Movement clip,
    // so a legacy-only publication can be cross-tabulated with the selection.
    void NoteDecisionMovementSelection(bool movement_selected);
    bool IsPlayerDecisionRefreshDue(football::sim::Tick now,
                                    football::sim::TickSpan cadence) const {
      return playerDecisionScheduler.Due(now, cadence);
    }
    void PublishPlayerDecisionQueue(const PlayerCommandQueue &commands,
                                    football::sim::Tick now);
    // 4f-a3a: independent Player Decision Clock and its serialized latest queue.
    bool HasPlayerDecisionQueue() const { return playerDecisionQueue.initialized; }
    const PlayerCommandQueue &GetPlayerDecisionQueue() const {
      return playerDecisionQueue.commands;
    }
    unsigned long long GetPlayerDecisionGeneration() const {
      return playerDecisionQueue.generation;
    }
    bool PublishMovementIntentFromQueue(const PlayerCommandQueue &queue, football::sim::Tick now, bool retaining_ball);
    bool LocomotionIntentRefreshHeldIneligible(football::sim::Tick now, bool retaining_ball) const;
    // H3d2a: Humanoid invokes these for completed action selection and ticks.
    // The executor advances independently; legacy state is an oracle only.
    void BeginSimulationAction();
    void StepSimulationAction(football::sim::TickSpan elapsed);
    // Validates the authoritative schedule against Humanoid's temporary
    // legacy projection. It is intentionally a fatal invariant in all builds.
    void CheckSimulationActionOracle() const;

    void TripMe(const Vector3 &tripVector, int tripType, const Player* ball_retainer) { humanoid->TripMe(tripVector, tripType, ball_retainer); }

    void RequestCommand(PlayerCommandQueue &commandQueue, const PlayerCommandInputs& inputs);
    int GetReactionTime_ms();
    float GetControlSpeed();
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

    // Tick-local borrows; no history owner or persistent world port.
    void Process(const football::sim::PlayerTickContext& tick, std::span<MentalImage> history, football::sim::BallTouchSink& touch_sink, football::sim::PlayerRuntimeSink& runtime_sink);


    float GetStat(football::model::PlayerStat name) const;
    float GetVelocityMultiplier() const;
    float GetMaxVelocity() const;

    const Anim *GetCurrentAnim() { return humanoid->GetCurrentAnim(); }

    void SetLastTouchTick(football::sim::Tick tick) { last_touch_tick_ = tick; }
    football::sim::Tick GetLastTouchTick() const { return last_touch_tick_; }
    void SetLastTouchType(e_TouchType touchType) { this->lastTouchType = touchType; }
    e_TouchType GetLastTouchType() { return lastTouchType; }
    // The ability-dependent decay is a continuous estimate, not a grid deadline.
    float GetLastTouchBias(int decay_ms, football::sim::Tick at);


    float GetFatigueFactorInv() const { return fatigueFactorInv; }
    void RelaxFatigue(float howMuch) {
      fatigueFactorInv += howMuch;
      fatigueFactorInv = clamp(fatigueFactorInv, 0.01f, 1.0f);
    }

    void ResetSituation(const Vector3 &focusPos, football::sim::Tick now);

    Humanoid *CastHumanoid();
    int GetTeamID() const;
    Team *GetTeam() const;
    Vector3 GetPitchPosition();
    bool TouchPending() const { return GetSimulationActionState().IsContactPending(); }
    bool TouchAnim() const { return GetSimulationActionState().HasScheduledContact(); }
    Vector3 GetTouchPos() const { return GetSimulationActionState().contactPosition; }
    int GetTouchFrame() const { return GetSimulationActionState().ContactFrame(); }
    int GetCurrentFrame() const { return GetSimulationActionState().Frame(); }
    void SelectRetainAnim() { humanoid->SelectRetainAnim(); }
    FormationEntry GetFormationEntry();
    void SetDynamicFormationEntry(FormationEntry entry) { dynamicFormationEntry = entry; }
    FormationEntry GetDynamicFormationEntry() { return dynamicFormationEntry; }
    bool HasPossession() const;
    bool HasBestPossession() const;
    bool HasUniquePossession() const;
    int GetTimeNeededToGetToBall_ms() const { return timeNeededToGetToBall_ms; }
    int GetTimeNeededToGetToBall_optimistic_ms() const { return timeNeededToGetToBall_optimistic_ms; }
    int GetTimeNeededToGetToBall_previous_ms() const { return timeNeededToGetToBall_previous_ms; }
    void SetDesiredTimeToBall_ms(int ms) { desiredTimeToBall_ms = ms; }
    int GetDesiredTimeToBall_ms() const { return clamp(desiredTimeToBall_ms, timeNeededToGetToBall_ms, 1000000.0f); }
    bool AllowLastDitch(bool includingPossessionAmount = true) const;
    void TriggerControlledBallCollision() { triggerControlledBallCollision = true; }
    bool IsControlledBallCollisionTriggered() { return triggerControlledBallCollision; }
    void ResetControlledBallCollisionTrigger() { triggerControlledBallCollision = false; }
    void UpdatePossessionStats(football::ball::Ball& ball, const Team& opponent, football::sim::Tick now, const Player* retainer);
    float GetClosestOpponentDistance(Team& opponent) const;
    const TacticalPlayerSituation &GetTacticalSituation() { return tacticalSituation; }
    void GiveYellowCard(football::sim::Tick effective_tick) { cards++; card_effective_tick_ = effective_tick; }
    void GiveRedCard(football::sim::Tick effective_tick) { cards += 3; card_effective_tick_ = effective_tick; }
    bool HasCards() { return cards > 0; }
    void SendOff(const football::ball::Ball& ball, football::sim::Tick now, blunted::Rng& rng);
    float GetStaminaStat() const;

  private:
    void ResetRuntimeState(const Vector3 &focusPos, football::sim::Tick now);
    void _CalculateTacticalSituation(const football::sim::PlayerTickContext& tick, std::span<MentalImage> history);
    void SynchronizeKinematicState();
    PlayerActionState CaptureLegacyActionState() const;
    void SetNextResetSituationAuditContext(int context);
    const AnimationLibrary& animations_;
    const football::model::Pitch& pitch_;
    blunted::Rng& rng_;

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
    PlayerDecisionScheduler playerDecisionScheduler;
    PlayerDecisionQueueState playerDecisionQueue;
    // Audit generation: bumped on every Direct publication for re-entry freshness
    // measurement. Deliberately transient so telemetry cannot change the save-state
    // contract; the gameplay epochs above are serialized instead.
    unsigned long long decisionLocomotionAuditGeneration = 0;
    std::optional<football::sim::Tick> last_locomotion_publication_tick_;
    std::optional<football::sim::Tick> last_reset_tick_;
    bool hasProcessedPlayerTick = false;
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
    int resetSituationAuditContext = kResetSituationUnspecified;
    std::optional<PlayerControl> control_;

    bool isActive = false;

    football::sim::Tick last_touch_tick_{};
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    Team *team = nullptr;
    FormationEntry dynamicFormationEntry;
    bool hasPossession = false;
    bool hasBestPossession = false;
    bool hasUniquePossession = false;
    unsigned int timeNeededToGetToBall_ms = 1000;
    unsigned int timeNeededToGetToBall_optimistic_ms = 1000;
    unsigned int timeNeededToGetToBall_previous_ms = 1000;
    bool triggerControlledBallCollision = false;
    TacticalPlayerSituation tacticalSituation;
    int desiredTimeToBall_ms = 0;
    int cards = 0; // 1: yellow; 2: second yellow; >=3: red
    football::sim::Tick card_effective_tick_{};

};

#endif

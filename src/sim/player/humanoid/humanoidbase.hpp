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

#ifndef _HPP_HUMANOIDBASE
#define _HPP_HUMANOIDBASE

#include "foundation/math/vector3.hpp"

#include "sim/gamedefines.hpp"
#include "sim/utils.hpp"

#include "animation/selection_query.hpp"
#include "animation/clip.hpp"

#include "sim/player/player_kinematics.hpp"

#include "sim/ai_support/mentalimage.hpp"

using namespace blunted;

// Library-side H3e4b diagnostics; shared with regression, never simulation state.
int &HumanoidProceduralMovementTicks();
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
// 4b''-player-path: telemetry is scoped by actor path on purpose. The previous
// round's counters mixed the real-player Process with the officials/base one.
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
// 4f-a2: whether two commands would drive SelectAnim identically. The field
// list is PlayerCommand::ProcessState(); floats and vectors compare bit-exact.
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
void RecordFootCounterfactual(int with_foot_head, int without_foot_head);
// 4f-c diagnostic A/B hook. Transient and disabled in normal gameplay; a
// restored branch can choose the alternative Movement foot order exactly once.
struct MovementAnimationPerturbation {
  bool enabled = false;
  bool require_frame_count_difference = false;
  bool applied = false;
  int player_id = -1;
  int time_ms = -1;
  int original_anim_id = -1;
  int alternative_anim_id = -1;
};
MovementAnimationPerturbation &MovementAnimationPerturbationAudit();
// 5a1: transient observation, never used to decide contact or serialized.
// A Ball::Touch request is not proof that Ball physics adopted that impulse.
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

class PlayerBase;
class Match;

enum e_InterruptAnim {
  e_InterruptAnim_None,
  e_InterruptAnim_Switch,
  e_InterruptAnim_Sliding,
  e_InterruptAnim_Bump,
  e_InterruptAnim_Trip,
  e_InterruptAnim_Cheat,
  e_InterruptAnim_Cancel,
  e_InterruptAnim_ReQueue
};

struct RotationSmuggle {
  RotationSmuggle() {
    begin = 0;
    end = 0;
  }
  void operator = (const float &value) {
    begin = value;
    end = value;
  }
  radian begin;
  radian end;
  void ProcessState(EnvState* state) {
    state->process(begin);
    state->process(end);
  }
};

struct Anim {
  AnimationId animationId = 0;
  int frameNum = 0;
  e_FunctionType functionType = e_FunctionType_None;
  e_InterruptAnim originatingInterrupt = e_InterruptAnim_None;
  Vector3 actionSmuggle;
  Vector3 actionSmuggleOffset;
  Vector3 actionSmuggleSustain;
  Vector3 actionSmuggleSustainOffset;
  Vector3 movementSmuggle;
  Vector3 movementSmuggleOffset;
  RotationSmuggle rotationSmuggle;
  radian rotationSmuggleOffset = 0;
  signed int touchFrame = -1;
  Vector3 touchPos;
  Vector3 incomingMovement;
  Vector3 outgoingMovement;
  Vector3 positionOffset;
  PlayerCommand originatingCommand;
  std::vector<Vector3> positions;
  void ProcessState(EnvState* state) {
    // Preserve the historical [id][bakedId] wire layout until the
    // simulation-state schema receives an explicit versioned migration.
    AnimationId legacy_id = animationId;
    state->process(legacy_id);
    state->process(animationId);
    if (state->Load()) {
      assert(legacy_id == animationId);
    }
    state->process(frameNum);
    state->process(functionType);
    state->process(originatingInterrupt);
    state->process(actionSmuggle);
    state->process(actionSmuggleOffset);
    state->process(actionSmuggleSustain);
    state->process(actionSmuggleSustainOffset);
    state->process(movementSmuggle);
    state->process(movementSmuggleOffset);
    rotationSmuggle.ProcessState(state);
    state->process(rotationSmuggleOffset);
    state->process(touchFrame);
    state->process(touchPos);
    state->process(incomingMovement);
    state->process(outgoingMovement);
    state->process(positionOffset);
    originatingCommand.ProcessState(state);
    state->process(positions);
  }
};

struct SpatialState {
  Vector3 position;
  radian angle;
  Vector3 directionVec; // for efficiency, vector version of angle
  e_Velocity enumVelocity;
  float floatVelocity = 0.0f; // for efficiency, float version

  Vector3 actualMovement;
  Vector3 physicsMovement; // ignores effects like positionoffset
  Vector3 animMovement;
  Vector3 movement; // one of the above (default)
  Vector3 actionSmuggleMovement;
  Vector3 movementSmuggleMovement;
  Vector3 positionOffsetMovement;

  radian bodyAngle;
  Vector3 bodyDirectionVec; // for efficiency, vector version of bodyAngle
  radian relBodyAngleNonquantized;
  radian relBodyAngle;
  Vector3 relBodyDirectionVec; // for efficiency, vector version of relBodyAngle
  Vector3 relBodyDirectionVecNonquantized;
  e_Foot foot;

  void Mirror() {
    position.Mirror();
    actualMovement.Mirror();
    physicsMovement.Mirror();
    animMovement.Mirror();
    movement.Mirror();
    actionSmuggleMovement.Mirror();
    movementSmuggleMovement.Mirror();
    positionOffsetMovement.Mirror();
  }

  void ProcessState(EnvState* state) {
    state->process(position);
    state->process(angle);
    state->process(directionVec);
    state->process(enumVelocity);
    state->process(floatVelocity);
    state->process(actualMovement);
    state->process(physicsMovement);
    state->process(animMovement);
    state->process(movement);
    state->process(actionSmuggleMovement);
    state->process(movementSmuggleMovement);
    state->process(positionOffsetMovement);
    state->process(bodyAngle);
    state->process(bodyDirectionVec);
    state->process(relBodyAngleNonquantized);
    state->process(relBodyAngle);
    state->process(relBodyDirectionVec);
    state->process(relBodyDirectionVecNonquantized);
    state->process(foot);
  }
};

class HumanoidBase {

  public:
    HumanoidBase(PlayerBase *player, Match *match);
    virtual ~HumanoidBase();
    void Mirror();

    virtual void Process();

    inline int GetFrameNum() { return currentAnim.frameNum; }
    inline int GetFrameCount() { return static_cast<int>(GetCurrentBakedClip().frame_count); }

    inline Vector3 GetPosition() const { return spatialState.position; }
    inline Vector3 GetDirectionVec() const { return spatialState.directionVec; }
    inline Vector3 GetBodyDirectionVec() const {
      return spatialState.bodyDirectionVec;
    }
    // Quantized relBodyAngle is animation-selection compatibility only.
    inline radian GetRelBodyAngle() const { return spatialState.relBodyAngle; }
    inline radian GetRelBodyAngleNonquantized() const {
      return spatialState.relBodyAngleNonquantized;
    }
    inline e_Velocity GetEnumVelocity() const { return spatialState.enumVelocity; }
    inline e_FunctionType GetCurrentFunctionType() const { return currentAnim.functionType; }
    inline e_FunctionType GetPreviousFunctionType() const { return previousAnim_functionType; }
    inline Vector3 GetMovement() const { return spatialState.movement; }

    int GetIdleMovementAnimID();
    void ResetPosition(const Vector3 &newPos, const Vector3 &focusPos);
    void OffsetPosition(const Vector3 &offset);
    void TripMe(const Vector3 &tripVector, int tripType);

    virtual float GetDecayingPositionOffsetLength() const { return decayingPositionOffset.GetLength(); }
    virtual float GetDecayingDifficultyFactor() const { return decayingDifficultyFactor; }

    const Anim *GetCurrentAnim() { return &currentAnim; }

    // Baked clip access. The runtime never reads legacy Animation objects;
    // these resolve through the stable AnimationId into the read-only library.
    const AnimationClip &GetBakedClip(AnimationId id) const;
    const AnimationClip &GetCurrentBakedClip() const;
    // Per-animation scratch for the selection sort predicates (replaces the
    // legacy mutable Animation::order_float field).
    float &OrderScratch(int id) const;

    virtual void ResetSituation(const Vector3 &focusPos);
    void ProcessState(EnvState* state);

  protected:
    bool _HighOrBouncyBall() const;
    void _KeepBestDirectionAnims(DataSet& dataset, const PlayerCommand &command, bool strict = true, radian allowedAngle = 0, int allowedVelocitySteps = 0, int forcedQuadrantID = -1); // ALERT: set sorting predicates before calling this function. strict kinda overrules the allowedstuff
    void _KeepBestBodyDirectionAnims(DataSet& dataset, const PlayerCommand &command, bool strict = true, radian allowedAngle = 0); // ALERT: set sorting predicates before calling this function. strict kinda overrules the allowedstuff
    virtual bool SelectAnim(const PlayerCommand &command, e_InterruptAnim localInterruptAnim, bool preferPassAndShot = false); // returns false on no applicable anim found
    void CalculatePredictedSituation(Vector3 &predictedPos, radian &predictedAngle);
    Vector3 CalculateOutgoingMovement(const std::vector<Vector3> &positions) const;

    void CalculateSpatialState(); // realtime properties, based on 'physics'
    void CalculateFactualSpatialState(); // realtime properties, based on anim. usable at last frame of anim. more riggid than above function
    // Reverse projection used by the locomotion authority flip: the simulation
    // movement state is the source and the legacy Humanoid movement fields
    // follow it. Action selection, the ball algorithms and the body pose all
    // read spatialState, so it must never keep a private animation position.
    // Animation-owned bookkeeping (actualMovement, physicsMovement,
    // animMovement, the smuggle movements and the foot) is deliberately left
    // alone until H3e4 takes it over.
    void ApplySimulationMovementState(const PlayerKinematicState &state);
    // One-way body-orientation compatibility projection. Continuous fields are
    // derived exactly from state.bodyFacing; quantized relBody* exists only for
    // legacy animation selection and never feeds bodyFacing back.
    void ApplySimulationBodyState(const PlayerKinematicState &state);
    // Produce this tick's movement. Pure locomotion is solved by the
    // simulation and the legacy Humanoid fields follow it; every other tick
    // keeps the legacy animation root motion and is merely projected. The
    // tick-start state is passed in so the procedural model integrates from
    // the world state in force, and so an action selection later in the same
    // tick cannot change what this tick's locomotion was.
    void ProjectMovementState(const PlayerKinematicState &tickStartState);
    bool UsesProceduralLocomotion() const;

    void AddTripCommandToQueue(PlayerCommandQueue &commandQueue, const Vector3 &tripVector, int tripType);
    PlayerCommand GetTripCommand(const Vector3 &tripVector, int tripType);
    PlayerCommand GetBasicMovementCommand(const Vector3 &desiredDirection, float velocityFloat);

    void SetFootSimilarityPredicate(e_Foot desiredFoot) const;
    bool CompareFootSimilarity(e_Foot foot, int animIndex1, int animIndex2) const;
    void SetIncomingVelocitySimilarityPredicate(e_Velocity velocity) const;
    bool CompareIncomingVelocitySimilarity(int animIndex1, int animIndex2) const;
    void SetMovementSimilarityPredicate(const Vector3 &relDesiredDirection, e_Velocity desiredVelocity) const;
    float GetMovementSimilarity(int animIndex, const Vector3 &relDesiredDirection, e_Velocity desiredVelocity, float corneringBias) const;
    bool CompareMovementSimilarity(int animIndex1, int animIndex2) const;
    bool CompareByOrderFloat(int animIndex1, int animIndex2) const;
    void SetIncomingBodyDirectionSimilarityPredicate(
        const Vector3 &relIncomingBodyDirection) const;
    bool CompareIncomingBodyDirectionSimilarity(int animIndex1, int animIndex2) const;
    void SetBodyDirectionSimilarityPredicate(const Vector3 &lookAt) const;
    real DirectionSimilarityRating(int animIndex) const;
    bool CompareBodyDirectionSimilarity(int animIndex1, int animIndex2) const;
    void SetTripDirectionSimilarityPredicate(const Vector3 &relDesiredTripDirection) const;
    bool CompareTripDirectionSimilarity(int animIndex1, int animIndex2) const;
    bool CompareBaseanimSimilarity(int animIndex1, int animIndex2) const;
    bool CompareCatchOrDeflect(int animIndex1, int animIndex2) const;
    void SetIdlePredicate(float desiredValue) const;
    bool CompareIdleVariable(int animIndex1, int animIndex2) const;
    bool ComparePriorityVariable(int animIndex1, int animIndex2) const;

    Vector3 CalculatePhysicsVector(int animID, bool useDesiredMovement,
                                  const Vector3 &desiredMovement,
                                  bool useDesiredBodyDirection,
                                  const Vector3 &desiredBodyDirectionRel,
                                  std::vector<Vector3> &positions_ret,
                                  radian &rotationOffset_ret) const;

    Vector3 ForceIntoAllowedBodyDirectionVec(const Vector3 &src) const;
    radian ForceIntoAllowedBodyDirectionAngle(radian angle) const; // for making small differences irrelevant while sorting
    Vector3 ForceIntoPreferredDirectionVec(const Vector3 &src) const;
    radian ForceIntoPreferredDirectionAngle(radian angle) const;

    Match *match;
    PlayerBase *player;
    // Shared between all players, no need to snapshot.
    // Seems to contain current animation context.

    Anim currentAnim;
    int previousAnim_frameNum;
    e_FunctionType previousAnim_functionType = e_FunctionType_None;

    // position/rotation offsets at the start of currentAnim
    Vector3 startPos;
    radian startAngle;

    // position/rotation offsets at the end of currentAnim
    Vector3 nextStartPos;
    radian nextStartAngle;

    // realtime info
    SpatialState spatialState;

    Vector3 previousPosition2D;

    e_InterruptAnim interruptAnim;
    int reQueueDelayFrames = 0;
    int tripType = 0;
    Vector3 tripDirection;

    Vector3 decayingPositionOffset;
    float decayingDifficultyFactor = 0.0f;

    // for comparing dataset entries (needed by std::list::sort)
    mutable e_Foot predicate_DesiredFoot;
    mutable e_Velocity predicate_IncomingVelocity;
    mutable Vector3 predicate_RelDesiredDirection;
    mutable Vector3 predicate_DesiredDirection;
    mutable float predicate_CorneringBias = 0.0f;
    mutable e_Velocity predicate_DesiredVelocity;
    mutable Vector3 predicate_RelIncomingBodyDirection;
    mutable Vector3 predicate_LookAt;
    mutable Vector3 predicate_RelDesiredTripDirection;
    mutable Vector3 predicate_RelDesiredBallDirection;
    mutable float predicate_idle = 0.0f;
    mutable std::vector<float> orderScratch_;

    // Should be dynamically retrieved from match, don't cache.
    int mentalImageTime = 0;

};

// Called at the single point where a selection replaces the command in force,
// so the recorded reason is the real scheduler decision and not a replay.
void RecordMovementCommandAcceptance(bool material_candidate,
                                     e_FunctionType previous_type,
                                     int previous_elapsed_ms,
                                     e_InterruptAnim interrupt,
                                     const PlayerCommand &command,
                                     int frame_count);

#endif

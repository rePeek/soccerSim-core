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

#include "../../../base/math/vector3.hpp"

#include "../../../gamedefines.hpp"
#include "../../../utils.hpp"

#include "animcollection.hpp"

#include "../player_kinematics.hpp"

#include "../../AIsupport/mentalimage.hpp"

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
void RecordFootCounterfactual(int with_foot_head, int without_foot_head,
                              AnimCollection *anims);
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
  RotationSmuggle() { DO_VALIDATION;
    begin = 0;
    end = 0;
  }
  void operator = (const float &value) { DO_VALIDATION;
    begin = value;
    end = value;
  }
  radian begin;
  radian end;
  void ProcessState(EnvState* state) { DO_VALIDATION;
    state->process(begin);
    state->process(end);
  }
};

struct Anim {
  Animation *anim = 0;
  signed int id = 0;
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
  void ProcessState(EnvState* state) { DO_VALIDATION;
    state->process(anim);
    state->process(id);
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

  void Mirror() { DO_VALIDATION;
    position.Mirror();
    actualMovement.Mirror();
    physicsMovement.Mirror();
    animMovement.Mirror();
    movement.Mirror();
    actionSmuggleMovement.Mirror();
    movementSmuggleMovement.Mirror();
    positionOffsetMovement.Mirror();
  }

  void ProcessState(EnvState* state) { DO_VALIDATION;
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
    HumanoidBase(PlayerBase *player, Match *match, boost::shared_ptr<AnimCollection> animCollection);
    virtual ~HumanoidBase();
    void Mirror();

    virtual void Process();

    inline int GetFrameNum() { DO_VALIDATION; return currentAnim.frameNum; }
    inline int GetFrameCount() { DO_VALIDATION; return currentAnim.anim->GetFrameCount(); }

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

    const Anim *GetCurrentAnim() { DO_VALIDATION; return &currentAnim; }
    const PlayerCommand &GetOriginatingCommand() const {
      DO_VALIDATION;
      return currentAnim.originatingCommand;
    }

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

    Vector3 CalculatePhysicsVector(Animation *anim, bool useDesiredMovement,
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
    boost::shared_ptr<AnimCollection> anims;
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
                                     const Animation *anim);

#endif

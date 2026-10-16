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

#include "sim/player/player_definition.hpp"
#include "football/foundation/math/vector3.hpp"

#include "sim/player/player_command.hpp"
#include "sim/player/player_tick_context.hpp"

#include "football/animation/selection_query.hpp"
#include "football/animation/clip.hpp"
#include "sim/player/player_baked_pose_sample.hpp"
#include "sim/player/player_selected_action.hpp"

#include "sim/player/player_kinematics.hpp"
#include "sim/observation/mentalimage.hpp"
#include "football/foundation/math/rng.hpp"

// ANIM-1 A9.4: the execution state, spatial state, interrupt reasons and the
// library-side diagnostics used to be defined in this header. They now live in
// their own Player-owned headers so nothing has to include a Humanoid header
// to name them.
#include "sim/player/player_action_execution_state.hpp"
#include "sim/player/player_diagnostics.hpp"
#include "sim/player/player_interrupt.hpp"
#include "sim/player/player_spatial_state.hpp"

#include <chrono>
#include <span>

namespace football::sim { class AcceptedTouchSink; class PlayerRuntimeSink; }

using namespace football::math;

class AnimationLibrary;

class HumanoidBase {
  // ANIM-1 A6.4 step 9: the Player owns action selection and installation; the
  // Humanoid remains a compatibility provider of the not-yet-migrated facts.
  friend class Player;


  public:
    HumanoidBase(Player *player, const AnimationLibrary& animations, football::math::Rng& rng);
    virtual ~HumanoidBase();
    void Mirror();

    // Tick-local borrows: observation history and the write-only touch port.
    virtual void Process(football::sim::Tick now, const football::sim::PlayerTickContext& tick,
                        std::span<MentalImage> history, football::sim::AcceptedTouchSink& touch_sink,
                        football::sim::PlayerRuntimeSink& runtime_sink);

    inline int GetFrameNum() { return currentAnim.frameNum; }
    inline void SetCompatFrameNum(int frame) { currentAnim.frameNum = frame; }
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
    // Legacy bookkeeping for a correction whose authority is the Player: it
    // updates the animation/motion origin (startPos, previous position, smuggle
    // offsets) without re-deriving the Player state.
    void ApplyPositionOffset(const Vector3 &offset);

    virtual float GetDecayingPositionOffsetLength() const { return decayingPositionOffset.GetLength(); }
    virtual float GetDecayingDifficultyFactor() const { return decayingDifficultyFactor; }

    football::sim::SelectedAction CurrentSelectedAction() const;
    const Anim *GetCurrentAnim() { return &currentAnim; }

    // Baked clip access. The runtime never reads legacy Animation objects;
    // these resolve through the stable AnimationId into the read-only library.
    const AnimationClip &GetBakedClip(AnimationId id) const;
    const AnimationClip &GetCurrentBakedClip() const;
    PlayerBakedPoseSample SampleBakedPose(const PlayerKinematicState& state, const football::player::PlayerBodyProfile& profile) const {
      const auto& clip = GetCurrentBakedClip();
      if (clip.poses.empty() || currentAnim.frameNum < 0)
        throw std::logic_error("missing runtime baked pose frame");
      const auto frame = std::min(std::size_t(currentAnim.frameNum),clip.poses.size()-1);
      PlayerBakedPoseSample sample;
      // ANIM-1 A5: the Humanoid only supplies clip/frame metadata; the final
      // pose and base yaw come from the Player's playback state
      // (Player::GetBakedPoseSample overrides them).
      sample.pose = clip.poses[frame]; sample.state = state;
      sample.animation_id = currentAnim.animationId;
      sample.requested_frame = currentAnim.frameNum; sample.sampled_frame = int(frame);
      sample.frame_clamped = sample.requested_frame != sample.sampled_frame;
      sample.base_angle = startAngle + currentAnim.rotationSmuggleOffset;
      sample.body_profile = profile; sample.action = currentAnim.functionType;
      return sample;
    }
    // Per-animation scratch for the selection sort predicates (replaces the
    // legacy mutable Animation::order_float field).
    float &OrderScratch(int id) const;

    virtual void ResetSituation(const Vector3 &focusPos);

  protected:
    bool _HighOrBouncyBall(const football::ball::Ball& ball) const;
    void _KeepBestDirectionAnims(DataSet& dataset, const PlayerCommand &command, bool strict = true, radian allowedAngle = 0, int allowedVelocitySteps = 0, int forcedQuadrantID = -1); // ALERT: set sorting predicates before calling this function. strict kinda overrules the allowedstuff
    void _KeepBestBodyDirectionAnims(DataSet& dataset, const PlayerCommand &command, bool strict = true, radian allowedAngle = 0); // ALERT: set sorting predicates before calling this function. strict kinda overrules the allowedstuff
    void CalculatePredictedSituation(Vector3 &predictedPos, radian &predictedAngle);
    Vector3 CalculateOutgoingMovement(const std::vector<Vector3> &positions) const;

    void CalculateSpatialState(const Player* ball_retainer); // realtime properties, based on 'physics'
    // Reverse projection used by the locomotion authority flip: the simulation
    // movement state is the source and the legacy Humanoid movement fields
    // follow it. Action selection, the ball algorithms and the body pose all
    // read spatialState, so it must never keep a private animation position.
    // Animation-owned bookkeeping (actualMovement, physicsMovement,
    // animMovement, the smuggle movements and the foot) is deliberately left
    // alone until H3e4 takes it over.
    void ApplySimulationMovementState(const PlayerKinematicState &state, const Player* ball_retainer);
    // S0.1: the projection half of the above, without adopting the projected
    // state back into the Player. The pure-locomotion path uses this: the Player
    // already computed and committed that state, so adopting it back would make
    // the Humanoid the authority again for that tick.
    void ProjectSimulationMovementState(const PlayerKinematicState &state, const Player* ball_retainer);
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
    void ProjectMovementState(const PlayerKinematicState &tickStartState,
                             const Player* ball_retainer,
                             bool restart_needs_simulation);
    bool UsesProceduralLocomotion(const Player* ball_retainer) const;


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

    Vector3 CalculatePhysicsVector(football::sim::Tick now, int animID, bool useDesiredMovement,
                                  const Vector3 &desiredMovement,
                                  bool useDesiredBodyDirection,
                                  const Vector3 &desiredBodyDirectionRel,
                                  std::vector<Vector3> &positions_ret,
                                  radian &rotationOffset_ret) const;


    const AnimationLibrary& animations_;
    football::math::Rng& rng_;
    Player *player;
    // Shared between all players, no need to snapshot.
    // Seems to contain current animation context.

    // ANIM-1 A9 step 1: the animation/spatial storage is Player-owned; the
    // Humanoid holds references so its remaining compatibility code is a view,
    // not a second authority.
    Anim &currentAnim;
    int previousAnim_frameNum;
    e_FunctionType previousAnim_functionType = e_FunctionType_None;


    // position/rotation offsets at the start of currentAnim
    Vector3 startPos;
    radian startAngle;

    // position/rotation offsets at the end of currentAnim
    Vector3 nextStartPos;
    radian nextStartAngle;

    // realtime info
    SpatialState &spatialState;

    // S0.3: legacy animation cache only, NOT a second world-position authority.
    // Written by the state projection and by the legacy reset/root-motion paths,
    // and read in exactly one place: CalculateSpatialState derives the legacy
    // `actualMovement` from (position - previousPosition2D) on the root-motion
    // branch. The pure-locomotion branch never reads it, so it cannot become a
    // hidden source of motion for the authority the Player now owns.
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

    // Reaction-history delay, not a timeline instant or a grid deadline.
    // Requeue observes the previous delay; action selection uses this tick's delay.
    // Keep sub-tick precision until the observation history nearest-capture sampling boundary.
    std::chrono::milliseconds mentalImageTime{0};

};

#endif

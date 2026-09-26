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
#include "../../data/playerdata.hpp"
#include "controller/icontroller.hpp"
#include "../../onthepitch/humangamer.hpp"

#include "../../scene/scene3d/node.hpp"

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
    virtual void Activate(boost::intrusive_ptr<Node> humanoidSourceNode, boost::shared_ptr<AnimCollection> animCollection, bool lazyPlayer) = 0;
    // go back to bench/take a shower
    virtual void Deactivate();

    void ResetPosition(const Vector3 &newPos, const Vector3 &focusPos);
    void OffsetPosition(const Vector3 &offset);

    inline int GetFrameNum() { DO_VALIDATION; return humanoid->GetFrameNum(); }
    inline int GetFrameCount() { DO_VALIDATION; return humanoid->GetFrameCount(); }

    // Authoritative movement state. `kinematicState` mirrors the Humanoid
    // spatial state and is refreshed by HumanoidBase whenever that state
    // changes (see CalculateSpatialState / CalculateFactualSpatialState /
    // OffsetPosition), so it is never a tick behind mid-tick readers such as
    // the controller. It is mirrored with the same semantics as the legacy
    // spatial state (position/velocity mirrored, facing not). Humanoid no
    // longer reads these getters for its own simulation, so this is not
    // circular. Actors must be positioned through PlayerBase::ResetPosition /
    // OffsetPosition so that this state cannot be left stale.
    inline Vector3 GetPosition() const { return kinematicState.position; }
    inline Vector3 GetGeomPosition() const { return humanoid->GetGeomPosition(); }
    inline Vector3 GetDirectionVec() const { return kinematicState.facing; }
    inline Vector3 GetBodyDirectionVec() const { return humanoid->GetBodyDirectionVec(); }
    inline Vector3 GetMovement() const { return kinematicState.velocity; }
    inline radian GetRelBodyAngle() const {
      return humanoid->GetRelBodyAngle();
    }
    inline e_Velocity GetEnumVelocity() const { return humanoid->GetEnumVelocity(); }
    inline float GetFloatVelocity() const {
      return EnumToFloatVelocity(humanoid->GetEnumVelocity());
    }
    inline e_FunctionType GetCurrentFunctionType() const { return humanoid->GetCurrentFunctionType(); }
    inline e_FunctionType GetPreviousFunctionType() const { return humanoid->GetPreviousFunctionType(); }
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
    const PlayerActionState &GetActionState() const {
      DO_VALIDATION;
      return actionState;
    }
    const PlayerActionState &GetActionExecutorShadow() const {
      DO_VALIDATION;
      return actionExecutorShadow;
    }

    void TripMe(const Vector3 &tripVector, int tripType) { DO_VALIDATION; humanoid->TripMe(tripVector, tripType); }

    void RequestCommand(PlayerCommandQueue &commandQueue);
    IController *GetController();
    void SetExternalController(HumanGamer *externalController);
    HumanController *ExternalController();
    bool ExternalControllerActive();

    boost::intrusive_ptr<Node> GetHumanoidNode() { DO_VALIDATION; return humanoid->GetHumanoidNode(); }

    // Validation helper for the deterministic regression. The kinematic mirror
    // must never disagree with the legacy Humanoid spatial state, else a
    // mid-tick reader (the controller is queried after the humanoid has already
    // advanced its spatial state) observes a stale actor. Delete together with
    // the Humanoid.
    bool IsKinematicMirrorConsistent() const {
      DO_VALIDATION;
      return kinematicState.position.GetDistance(humanoid->GetPosition()) < 1e-4f &&
             kinematicState.velocity.GetDistance(humanoid->GetMovement()) < 1e-4f &&
             kinematicState.facing.GetDistance(humanoid->GetDirectionVec()) < 1e-4f;
    }

    float GetDecayingPositionOffsetLength() { DO_VALIDATION; return humanoid->GetDecayingPositionOffsetLength(); }

    virtual void Process();
    virtual void PreparePutBuffers();
    virtual void FetchPutBuffers();


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
    void SynchronizeActionState();
    void BeginActionExecutorShadow();
    void ResetActionExecutorShadow();
    void UpdateActionExecutorShadow();
    Match *match;

    const PlayerData* const playerData;
    const int stable_id = 0;

    std::unique_ptr<HumanoidBase> humanoid;
    PlayerKinematicState kinematicState;
    PlayerKinematicState kinematicShadow;
    PlayerGroundCollider groundCollider;
    PlayerActionState actionState;
    PlayerActionState actionExecutorShadow;
    int actionExecutorShadowAnimationId = -1;
    std::unique_ptr<IController> controller;
    HumanGamer *externalController = 0;

    bool isActive = false;

    unsigned long lastTouchTime_ms = 0;
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    std::vector<Vector3> positionHistoryPerSecond; // resets too (on ResetSituation() calls)

};

#endif

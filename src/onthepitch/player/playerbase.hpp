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

    inline int GetFrameNum() { DO_VALIDATION; return humanoid->GetFrameNum(); }
    inline int GetFrameCount() { DO_VALIDATION; return humanoid->GetFrameCount(); }

    // Gameplay-facing movement state. It is NOT yet the producer: Humanoid's
    // spatial state is still authoritative for movement, and `kinematicState`
    // is an exact mirror of it. H3e flips this producer relationship. Every
    // mutation of the Humanoid spatial state refreshes the mirror at the
    // mutation point (CalculateSpatialState, CalculateFactualSpatialState,
    // OffsetPosition, Mirror, state restore), so mid-tick readers such as the
    // controller never see a stale actor. Mirroring follows the legacy
    // field-level asymmetry (position/velocity mirrored, facing not).
    // CheckSimulationKinematicOracle() enforces the mirror bit-exactly.
    // Actors must be positioned through PlayerBase::ResetPosition /
    // OffsetPosition so that this state cannot be left stale.
    inline Vector3 GetPosition() const { return kinematicState.position; }
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
    inline e_FunctionType GetCurrentFunctionType() const {
      return GetSimulationActionState().type;
    }
    inline e_FunctionType GetPreviousFunctionType() const { return humanoid->GetPreviousFunctionType(); }
    // H3e authority boundary: true only when this actor may leave the legacy
    // animation root-motion path and use the simulation-owned procedural
    // movement model instead. See PlayerActionState::IsPureLocomotion().
    bool IsEligibleForProceduralLocomotion() const;
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

    // Bit-exact mirror check: position, velocity, facing, derived speed and the
    // collider center must all match the Humanoid spatial state exactly. An
    // epsilon cannot mask a synchronization bug, so there is none.
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
    Match *match;

    const PlayerData* const playerData;
    const int stable_id = 0;

    std::unique_ptr<HumanoidBase> humanoid;
    PlayerKinematicState kinematicState;
    PlayerKinematicState kinematicShadow;
    PlayerGroundCollider groundCollider;
    PlayerActionState actionState;
    std::unique_ptr<IController> controller;
    HumanGamer *externalController = 0;

    bool isActive = false;

    unsigned long lastTouchTime_ms = 0;
    e_TouchType lastTouchType;

    float fatigueFactorInv = 0.0f;

    std::vector<Vector3> positionHistoryPerSecond; // resets too (on ResetSituation() calls)

};

#endif

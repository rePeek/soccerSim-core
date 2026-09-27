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

#include "player.hpp"
#include "player_action_executor.hpp"
#include "player_locomotion.hpp"
#include "legacy_locomotion_command.hpp"

#include <cstring>

#include "../match.hpp"

#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"

#include "../../main.hpp"
#include "../../utils.hpp"

#include "../../base/geometry/triangle.hpp"

namespace {

// Bit comparison, not an epsilon: a movement mirror that is only approximately
// right is a synchronization bug, and masking it would let a later procedural
// producer inherit an inconsistent baseline.
bool Vector3BitsEqual(const Vector3 &a, const Vector3 &b) {
  DO_VALIDATION;
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

bool FloatBitsEqual(float a, float b) {
  DO_VALIDATION;
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

}  // namespace

PlayerBase::PlayerBase(Match *match, PlayerData *playerData)
    : match(match),
      playerData(playerData),
      stable_id(GetContext().stablePlayerCount++) {
  DO_VALIDATION;
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  fatigueFactorInv = 1.0;
}

PlayerBase::~PlayerBase() {
  DO_VALIDATION;
  if (isActive) Deactivate();
}

bool PlayerBase::IsKinematicMirrorConsistent() const {
  DO_VALIDATION;
  return Vector3BitsEqual(kinematicState.position, humanoid->GetPosition()) &&
         Vector3BitsEqual(kinematicState.velocity, humanoid->GetMovement()) &&
         Vector3BitsEqual(kinematicState.facing, humanoid->GetDirectionVec()) &&
         Vector3BitsEqual(kinematicState.bodyFacing,
                          humanoid->GetBodyDirectionVec()) &&
         FloatBitsEqual(kinematicState.speed,
                        kinematicState.velocity.GetLength()) &&
         Vector3BitsEqual(groundCollider.center,
                          kinematicState.position.Get2D());
}

void PlayerBase::CheckSimulationKinematicOracle() const {
  DO_VALIDATION;
  const Vector3 &position = humanoid->GetPosition();
  const Vector3 &movement = humanoid->GetMovement();
  const Vector3 &direction = humanoid->GetDirectionVec();
  const Vector3 &bodyDirection = humanoid->GetBodyDirectionVec();

  std::string mismatch;
  if (!Vector3BitsEqual(kinematicState.position, position)) {
    mismatch = "position";
  } else if (!Vector3BitsEqual(kinematicState.velocity, movement)) {
    mismatch = "velocity";
  } else if (!Vector3BitsEqual(kinematicState.facing, direction)) {
    mismatch = "facing";
  } else if (!Vector3BitsEqual(kinematicState.bodyFacing, bodyDirection)) {
    mismatch = "body facing";
  } else if (!FloatBitsEqual(kinematicState.speed,
                             kinematicState.velocity.GetLength())) {
    mismatch = "speed";
  } else if (!Vector3BitsEqual(groundCollider.center, position.Get2D())) {
    mismatch = "collider center";
  }
  if (!mismatch.empty()) {
    Log(e_FatalError, "PlayerBase", "CheckSimulationKinematicOracle",
        "the gameplay kinematic mirror diverged from the Humanoid spatial "
        "state: " + mismatch);
  }
}

void PlayerBase::Mirror() {
  DO_VALIDATION;
  humanoid->Mirror();
  kinematicState.Mirror();
  kinematicShadow.Mirror();
  groundCollider.Mirror();
  CheckSimulationKinematicOracle();
}

void PlayerBase::SynchronizeKinematicState() {
  DO_VALIDATION;
  kinematicState.position = humanoid->GetPosition();
  kinematicState.velocity = humanoid->GetMovement();
  kinematicState.facing = humanoid->GetDirectionVec();
  kinematicState.bodyFacing = humanoid->GetBodyDirectionVec();
  kinematicState.speed = kinematicState.velocity.GetLength();
  groundCollider.SetCenter(kinematicState.position);
  CheckSimulationKinematicOracle();
}

PlayerActionState PlayerBase::CaptureLegacyActionState() const {
  DO_VALIDATION;
  PlayerActionState legacy;
  legacy.type = humanoid->GetCurrentFunctionType();
  legacy.frame = humanoid->GetFrameNum();
  legacy.frameCount = humanoid->GetFrameCount();
  legacy.elapsedTime_ms = legacy.frame * 10;
  legacy.durationTime_ms = legacy.frameCount * 10;
  const Anim *anim = humanoid->GetCurrentAnim();
  legacy.contactFrame = anim->touchFrame;
  legacy.contactTime_ms =
      anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  legacy.contactPosition = anim->touchPos;
  return legacy;
}

void PlayerBase::CheckSimulationActionOracle() const {
  DO_VALIDATION;
  const PlayerActionState legacy = CaptureLegacyActionState();
  const bool contactPositionMatches =
      std::memcmp(actionState.contactPosition.coords,
                  legacy.contactPosition.coords,
                  sizeof(actionState.contactPosition.coords)) == 0;
  std::string mismatch;
  if (actionState.type != legacy.type) {
    mismatch = "type";
  } else if (actionState.frame != legacy.frame) {
    mismatch = "frame";
  } else if (actionState.frameCount != legacy.frameCount) {
    mismatch = "frame count";
  } else if (actionState.elapsedTime_ms != legacy.elapsedTime_ms) {
    mismatch = "elapsed time";
  } else if (actionState.durationTime_ms != legacy.durationTime_ms) {
    mismatch = "duration";
  } else if (actionState.contactTime_ms != legacy.contactTime_ms) {
    mismatch = "contact time";
  } else if (actionState.contactFrame != legacy.contactFrame) {
    mismatch = "contact frame";
  } else if (!contactPositionMatches) {
    mismatch = "contact position bits";
  }
  if (!mismatch.empty()) {
    Log(e_FatalError, "PlayerBase", "CheckSimulationActionOracle",
        "authoritative action state diverged from legacy oracle: " + mismatch +
            " (simulation frame=" + std::to_string(actionState.frame) +
            ", legacy frame=" + std::to_string(legacy.frame) +
            ", simulation elapsed=" +
            std::to_string(actionState.elapsedTime_ms) +
            ", legacy elapsed=" + std::to_string(legacy.elapsedTime_ms) + ")");
  }
}

void PlayerBase::BeginSimulationAction() {
  DO_VALIDATION;
  const Anim *anim = humanoid->GetCurrentAnim();
  PlayerActionDefinition definition;
  definition.type = humanoid->GetCurrentFunctionType();
  definition.durationTime_ms = humanoid->GetFrameCount() * 10;
  definition.contactTime_ms = anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  definition.contactPosition = anim->touchPos;
  PlayerActionExecutor::Begin(actionState, definition);

  // ResetPosition can deliberately start an idle animation at a non-zero
  // legacy frame. Establish that initial cursor once; normal ticks never
  // derive executor time from Humanoid.
  const int initialElapsedTime_ms = humanoid->GetFrameNum() * 10;
  if (initialElapsedTime_ms > 0) {
    PlayerActionExecutor::Step(actionState, initialElapsedTime_ms);
  }
  CheckSimulationActionOracle();
}

void PlayerBase::StepSimulationAction(int elapsedTime_ms) {
  DO_VALIDATION;
  PlayerActionExecutor::Step(actionState, elapsedTime_ms);
  CheckSimulationActionOracle();
}

bool PlayerBase::IsEligibleForProceduralLocomotion() const {
  DO_VALIDATION;
  return actionState.IsPureLocomotion(match->GetBallRetainer() == this);
}

void PlayerBase::ResetKinematicShadow() {
  DO_VALIDATION;
  kinematicShadow = kinematicState;
}

void PlayerBase::UpdateKinematicShadow() {
  DO_VALIDATION;
  const PlayerCommand &command = humanoid->GetOriginatingCommand();
  if (GetCurrentFunctionType() != e_FunctionType_Movement ||
      !command.useDesiredMovement) {
    ResetKinematicShadow();
    return;
  }

  // The single place where a legacy command becomes a locomotion input. The
  // authoritative execution path reuses this when the authority flips, so
  // prediction, execution and this shadow can never drift apart semantically.
  // Keep the adapter's entire input in simulation state. bodyFacing is
  // simulation-authoritative for pure locomotion and an exact legacy shadow
  // otherwise; it never needs a direct Humanoid read here.
  const PlayerLocomotionInput input = BuildLegacyLocomotionInput(
      command, kinematicShadow, GetMaxVelocity(), kinematicShadow.bodyFacing);

  PlayerLocomotionParameters parameters;
  parameters.maxSpeed = GetMaxVelocity();
  PlayerLocomotion::Step(kinematicShadow, input, parameters, 0.01f);
}

void PlayerBase::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  DO_VALIDATION;
  humanoid->ResetPosition(newPos, focusPos);
  SynchronizeKinematicState();
  BeginSimulationAction();
  ResetKinematicShadow();
}

void PlayerBase::OffsetPosition(const Vector3 &offset) {
  DO_VALIDATION;
  humanoid->OffsetPosition(offset);
  SynchronizeKinematicState();
  CheckSimulationActionOracle();
  ResetKinematicShadow();
}

void PlayerBase::Deactivate() {
  DO_VALIDATION;
  ResetSituation(GetPosition());
  isActive = false;
  externalController = nullptr;
}

IController *PlayerBase::GetController() {
  DO_VALIDATION;
  if (ExternalControllerActive()) {
    return externalController->GetHumanController();
  } else {
    return controller.get();
  }
}

void PlayerBase::RequestCommand(PlayerCommandQueue &commandQueue) {
  DO_VALIDATION;
  if (ExternalControllerActive()) {
    externalController->GetHumanController()->RequestCommand(commandQueue);
  } else {
    controller->RequestCommand(commandQueue);
  }
}

void PlayerBase::SetExternalController(HumanGamer *externalController) {
  DO_VALIDATION;
  this->externalController = externalController;
  if (this->externalController) {
    DO_VALIDATION;
    this->externalController->GetHumanController()->Reset();
    this->externalController->GetHumanController()->SetPlayer(this);
  } else {
    controller->Reset();
  }
}

HumanController *PlayerBase::ExternalController() {
  DO_VALIDATION;
  return externalController ? externalController->GetHumanController() : nullptr;
}

bool PlayerBase::ExternalControllerActive() {
  DO_VALIDATION;
  return externalController && !externalController->GetHumanController()->Disabled();
}

void PlayerBase::Process() {
  DO_VALIDATION;
  if (isActive) {
    DO_VALIDATION;
    if (ExternalControllerActive()) externalController->GetHumanController()->Process(); else controller->Process();
    humanoid->Process();
    SynchronizeKinematicState();
    CheckSimulationActionOracle();
    UpdateKinematicShadow();
  }
}


float PlayerBase::GetStat(PlayerStat name) const {
  return playerData->GetStat(name);
}

float PlayerBase::GetMaxVelocity() const {
  // see humanoidbase's physics function
  return sprintVelocity * GetVelocityMultiplier();
}

float PlayerBase::GetVelocityMultiplier() const {
  // see humanoid_utils' physics function
  return 0.9f + playerData->get_physical_velocity() * 0.1f;
}

float PlayerBase::GetLastTouchBias(int decay_ms, unsigned long time_ms) {
  DO_VALIDATION;
  unsigned long adaptedTime_ms = time_ms;
  if (time_ms == 0) adaptedTime_ms = match->GetActualTime_ms();
  if (decay_ms > 0) return 1.0f - clamp((adaptedTime_ms - GetLastTouchTime_ms()) / (float)decay_ms, 0.0f, 1.0f);
  return 0.0f;
}

void PlayerBase::ResetSituation(const Vector3 &focusPos) {
  DO_VALIDATION;
  positionHistoryPerSecond.clear();
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  if (IsActive()) {
    humanoid->ResetSituation(focusPos);
    SynchronizeKinematicState();
    BeginSimulationAction();
    ResetKinematicShadow();
  }
  if (GetController()) GetController()->Reset();
}

void PlayerBase::ProcessStateBase(EnvState *state) {
  DO_VALIDATION;
  state->process(isActive);
  humanoid->ProcessState(state);
  kinematicState.ProcessState(state);
  kinematicShadow.ProcessState(state);
  groundCollider.ProcessState(state);
  // A restored Humanoid spatial state, kinematic mirror and collider must
  // agree before any subsequent tick or reader can observe either.
  CheckSimulationKinematicOracle();
  actionState.ProcessState(state);
  // After saving or loading the action state, require it to agree with the
  // Humanoid motion cursor before a subsequent tick can observe either.
  CheckSimulationActionOracle();
  if (IsActive()) {
    controller->ProcessState(state);
  }
  state->process(externalController);
  state->process(lastTouchTime_ms);
  state->process(lastTouchType);
  state->process(fatigueFactorInv);
  state->process(positionHistoryPerSecond);
}

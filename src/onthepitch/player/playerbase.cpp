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

#include "../match.hpp"

#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"

#include "../../main.hpp"
#include "../../utils.hpp"

#include "../../base/geometry/triangle.hpp"

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

void PlayerBase::Mirror() {
  humanoid->Mirror();
  kinematicState.Mirror();
  kinematicShadow.Mirror();
}

void PlayerBase::SynchronizeKinematicState() {
  DO_VALIDATION;
  kinematicState.position = humanoid->GetPosition();
  kinematicState.velocity = humanoid->GetMovement();
  kinematicState.facing = humanoid->GetDirectionVec();
  kinematicState.speed = kinematicState.velocity.GetLength();
}

void PlayerBase::SynchronizeActionState() {
  DO_VALIDATION;
  actionState.type = humanoid->GetCurrentFunctionType();
  actionState.frame = humanoid->GetFrameNum();
  actionState.frameCount = humanoid->GetFrameCount();
  actionState.elapsedTime_ms = actionState.frame * 10;
  actionState.durationTime_ms = actionState.frameCount * 10;
  const Anim *anim = humanoid->GetCurrentAnim();
  actionState.contactFrame = anim->touchFrame;
  actionState.contactTime_ms =
      anim->touchFrame == -1 ? -1 : anim->touchFrame * 10;
  actionState.contactPosition = anim->touchPos;
}

void PlayerBase::ResetKinematicShadow() {
  DO_VALIDATION;
  kinematicShadow = kinematicState;
}

void PlayerBase::UpdateKinematicShadow() {
  DO_VALIDATION;
  const PlayerCommand &command = humanoid->GetOriginatingCommand();
  if (humanoid->GetCurrentFunctionType() != e_FunctionType_Movement ||
      !command.useDesiredMovement) {
    ResetKinematicShadow();
    return;
  }

  PlayerKinematicInput input;
  const float desiredSpeed =
      clamp(command.desiredVelocityFloat, 0.0f, GetMaxVelocity());
  input.desiredVelocity =
      command.desiredDirection.Get2D().GetNormalized(kinematicShadow.facing) *
      desiredSpeed;
  input.desiredFacing = input.desiredVelocity.GetNormalized(
      kinematicShadow.facing);
  if (command.useDesiredLookAt) {
    input.desiredFacing =
        (command.desiredLookAt - kinematicShadow.position)
            .Get2D()
            .GetNormalized(input.desiredFacing);
  }

  PlayerKinematicParameters parameters;
  parameters.maxSpeed = GetMaxVelocity();
  PlayerKinematics::Step(kinematicShadow, input, parameters, 0.01f);
}

void PlayerBase::ResetPosition(const Vector3 &newPos, const Vector3 &focusPos) {
  DO_VALIDATION;
  humanoid->ResetPosition(newPos, focusPos);
  SynchronizeKinematicState();
  SynchronizeActionState();
  ResetKinematicShadow();
}

void PlayerBase::OffsetPosition(const Vector3 &offset) {
  DO_VALIDATION;
  humanoid->OffsetPosition(offset);
  SynchronizeKinematicState();
  SynchronizeActionState();
  ResetKinematicShadow();
}

void PlayerBase::Deactivate() {
  DO_VALIDATION;
  ResetSituation(GetPosition());

  if (humanoid) humanoid->Hide();
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
    SynchronizeActionState();
    UpdateKinematicShadow();
  } else {
    if (humanoid) humanoid->Hide();
  }
}

void PlayerBase::PreparePutBuffers() {
  DO_VALIDATION;
  humanoid->PreparePutBuffers();
}

void PlayerBase::FetchPutBuffers() {
  DO_VALIDATION;
  humanoid->FetchPutBuffers();
}

void PlayerBase::Put(bool mirror) {
  DO_VALIDATION;
  humanoid->Put(mirror);
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
    SynchronizeActionState();
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
  actionState.ProcessState(state);
  if (IsActive()) {
    controller->ProcessState(state);
  }
  state->process(externalController);
  state->process(lastTouchTime_ms);
  state->process(lastTouchType);
  state->process(fatigueFactorInv);
  state->process(positionHistoryPerSecond);
}

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


#include "player/kick_targeting.hpp"

#include <cassert>
#include <cmath>

#include "player/player.hpp"

namespace football::sim::mechanics {

namespace {

void CalculatePassDirectionAndPower(e_FunctionType passType, const Vector3 &vector,
                    Vector3 &resultingDirection, float &resultingPower) {
  float heightOffset = 0.11f;
  float powerFactor = 1.8f;//1.6f
  float distanceExp = 1.4f;
  if (passType == e_FunctionType_HighPass) {
    heightOffset = 0.45f - NormalizedClamp(vector.GetLength(), 0.0f, 60.0f) * 0.15f;// 0.37f;
    powerFactor = 1.15f;//1.75
    distanceExp = 1.4f;//1.6
  }
  resultingDirection = (vector.GetNormalized(0) + Vector3(0, 0, heightOffset)).GetNormalized(0);
  resultingPower =
      std::pow(NormalizedClamp(vector.GetLength(), 0.0f, 60.0f), distanceExp) *
      powerFactor;
}

}  // namespace

bool GetPass(Player *player, e_FunctionType passType,
             Player *forcedTargetPlayer,
             const Vector3 *forcedTargetPosition,
             Vector3 &resultingDirection, float &resultingPower,
             Player *&targetPlayer) {

  assert(forcedTargetPlayer != player);
  // A value control may outlive the recipient's activation until contact.
  if (forcedTargetPlayer && !forcedTargetPlayer->IsActive())
    forcedTargetPlayer = nullptr;

  // The target is always explicit. This solver only predicts the receiver's
  // lead, never chooses which teammate to play to.
  Vector3 playerPos = player->GetPosition() +
      player->GetMovement().GetNormalized(0) * 0.2f;
  if (player->TouchAnim()) {
    playerPos = player->GetTouchPos().Get2D();
  }

  targetPlayer = nullptr;
  Vector3 target;
  if (forcedTargetPlayer) {
    float passDuration =
        0.3f + (forcedTargetPlayer->GetPosition() - playerPos).GetLength() * 0.05f;
    passDuration = std::pow(clamp(passDuration, 0.0f, 1.0f), 0.7f) *
                   0.7f;  // after this time, the player is supposed to have
                          // been able to stop
    target = forcedTargetPlayer->GetPosition() +
             forcedTargetPlayer->GetMovement() * passDuration;
    targetPlayer = forcedTargetPlayer;
  } else if (forcedTargetPosition != nullptr) {
    target = *forcedTargetPosition;
  } else {
    return false;
  }

  Vector3 targetRel = target - playerPos;
  if (passType == e_FunctionType_LongPass) {
    targetRel += Vector3(
        -player->GetDefendingDirection() * targetRel.GetLength() * 0.2f, 0, 0);
  }
  CalculatePassDirectionAndPower(passType, targetRel, resultingDirection,
                                 resultingPower);
  return true;
}

}  // namespace football::sim::mechanics

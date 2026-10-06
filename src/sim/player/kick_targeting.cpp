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


#include "sim/player/kick_targeting.hpp"

#include <cmath>
#include <cassert>

#include "sim/team.hpp"
#include "sim/player/player.hpp"

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

void GetPass(Player *player, e_FunctionType passType,
                const Vector3 &inputDirection, float inputPower,
                float autoDirectionBias, float autoPowerBias,
                Vector3 &resultingDirection, float &resultingPower,
                Player *&targetPlayer, Player *forcedTargetPlayer) {

  // Assistance is specified by the executable request, never its input source.
  float adaptedAutoDirectionBias = autoDirectionBias;
  float adaptedAutoPowerBias = autoPowerBias;

  assert(forcedTargetPlayer != player);
  // A value control may outlive the recipient's activation until contact.
  if (forcedTargetPlayer && !forcedTargetPlayer->IsActive()) forcedTargetPlayer = nullptr;


  // find out what player we intend to play to

  Vector playerPos = player->GetPosition() + player->GetMovement().GetNormalized(0) * 0.2f; // + ffo. don't need movement for future stuff, since getpass is recalled at moment of passing, for refinement
  if (player->TouchAnim()) {
    playerPos = player->GetTouchPos().Get2D();
  }
  Vector3 manualTarget = playerPos + inputDirection * clamp(inputPower * 60.0f, 1.0f, 100.0f);

  std::vector<Player*> players;
  player->GetTeam()->GetActivePlayers(players);

  if (players.size() < 2) {
    resultingDirection = player->GetDirectionVec();
    resultingPower = 1.0f;
    targetPlayer = player;
    adaptedAutoDirectionBias = 0.0f;
    adaptedAutoPowerBias = 0.0f;
  }

  Player *bestTargetPlayer = player;
  Vector3 autoTarget = manualTarget;

  if (forcedTargetPlayer) {

    bestTargetPlayer = forcedTargetPlayer;

    float passDuration = 0.3f + (forcedTargetPlayer->GetPosition() - playerPos).GetLength() * 0.05f; // educated guess
    passDuration = std::pow(clamp(passDuration, 0.0f, 1.0f), 0.7f) *
                   0.7f;  // after this time, the player is supposed to have
                          // been able to stop

    autoTarget = forcedTargetPlayer->GetPosition() +
                 forcedTargetPlayer->GetMovement() * passDuration; // correct for pass duration

  } else {
    float bestRating = 10000;
    autoTarget = playerPos;

    for (int i = 0; i < (signed int)players.size(); i++) {
      if (players[i] != player /* && players[i]->IsActive()*/) {

        float passDuration = 0.3f + (players[i]->GetPosition() - playerPos).GetLength() * 0.05f; // educated guess
        passDuration = std::pow(clamp(passDuration, 0.0f, 1.0f), 0.7f) *
                       0.7f;  // after this time, the player is supposed to have
                              // been able to stop

        Vector3 targetPos = players[i]->GetPosition() +
                            players[i]->GetMovement() * passDuration; // correct for pass duration
        // rate
        float distanceRating =
            std::pow(NormalizedClamp((targetPos - manualTarget).GetLength(),
                                     0.0f, 70.0f),
                     0.8f) *
            0.8f;  // std::pow() this so small differences matter more - from some
                   // point on, it just doesn't really matter that much anymore
        float angleRating = std::fabs((targetPos - playerPos).GetNormalized(0).GetAngle2D(inputDirection) / (1.0f * pi)) * 1.0f;
        if (distanceRating + angleRating < bestRating) {
          bestRating = distanceRating + angleRating;
          bestTargetPlayer = players[i];
          autoTarget = targetPos;
        }
      }
    }
  }

  targetPlayer = bestTargetPlayer;
  assert(targetPlayer);

  Vector3 autoTargetRel = autoTarget - playerPos;
  Vector3 manualTargetRel = manualTarget - playerPos;

  if (forcedTargetPlayer) {
    adaptedAutoDirectionBias = 1.0;
    adaptedAutoPowerBias = 1.0;
  } else {
    // only help when at least somewhat close to target
    float maxAllowedDistance = 50.0f;
    float distanceFactor =
        1.0 - std::pow(clamp((autoTargetRel - manualTargetRel).GetLength() /
                                 maxAllowedDistance,
                             0.0f, 1.0f),
                       1.5f);
    // extra help with power on close targets (because people can only physicaly press the pass button so short)
    float proximityBonus =
        std::pow(1.0f - NormalizedClamp((playerPos - autoTarget).GetLength(),
                                        0.0f, 12.0f),
                 0.5f);

    adaptedAutoDirectionBias *= distanceFactor;
    adaptedAutoDirectionBias =
        std::pow(adaptedAutoDirectionBias, 1.0f - proximityBonus * 0.9f);
    adaptedAutoPowerBias *= distanceFactor;
    adaptedAutoPowerBias = clamp(adaptedAutoPowerBias * (1.0 + proximityBonus), 0.0f, 1.0f);
    //printf("adaptedAutoPowerBias: %f\n", adaptedAutoPowerBias);
  }

  Vector3 offset;
  if (passType == e_FunctionType_LongPass) {
    float targetDistance = autoTargetRel.GetLength()   * adaptedAutoDirectionBias +
                           manualTargetRel.GetLength() * (1.0f - adaptedAutoDirectionBias);
    offset = Vector3(
        -player->GetTeam()->GetDynamicSide() * targetDistance * 0.2f, 0, 0);
    autoTargetRel += offset;
    manualTargetRel += offset;
  }
  Vector3 resultingTargetRel = (autoTargetRel.GetNormalized(0) * adaptedAutoDirectionBias + manualTargetRel.GetNormalized(0) * (1.0f - adaptedAutoDirectionBias)).GetNormalized(manualTarget);
  resultingTargetRel *= float(autoTargetRel.GetLength() * adaptedAutoPowerBias + manualTargetRel.GetLength() * (1.0f - adaptedAutoPowerBias));

  CalculatePassDirectionAndPower(passType, resultingTargetRel, resultingDirection, resultingPower);
}

Vector3 GetShotDirection(Player *player, const Vector3 &inputDirection,
                            float autoDirectionBias) {

  Vector3 manualDirection = inputDirection;

  Vector3 goalPos =
      Vector3(player->GetTeam()->GetDynamicSide() * -pitchHalfW, 0, 0);
  Vector3 toGoal = (goalPos - (player->GetPosition() + player->GetMovement() * 0.12f)).GetNormalized(0);
  // if inputDirection ~== toGoal, it is considered as aiming 'through the middle'. so, get the deviation from inputDirection to toGoal, and make 90 degrees the maximum
  radian relAngle = toGoal.GetAngle2D(inputDirection);
  float sideFactor = clamp((relAngle / pi) / 0.5f, -1.0f, 1.0f);
  // more attenuation towards the sides
  sideFactor = std::pow(std::fabs(sideFactor), 0.7f) * signSide(sideFactor);

  goalPos.coords[1] =
      sideFactor * goalHalfWidth * 0.9f * player->GetTeam()->GetDynamicSide();
  Vector3 autoDirection = (goalPos - (player->GetPosition() + player->GetMovement() * 0.12f)).GetNormalized(0);

  return (manualDirection * (1.0f - autoDirectionBias) + autoDirection * autoDirectionBias).GetNormalized(inputDirection);
}

// get the offensiveness of a role

}  // namespace football::sim::mechanics

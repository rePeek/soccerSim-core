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


#include "ai/dribble_policy.hpp"

#include <cmath>

#include "ai/formation_policy.hpp"
#include "ai/positioning.hpp"
#include "sim/query/player_query.hpp"
#include "sim/ai_support/mentalimage.hpp"
#include "sim/match.hpp"
#include "sim/team.hpp"
#include "sim/player/player.hpp"

namespace football::ai {

void GetBestDribbleMovement(Match *match, Player *player,
                               const MentalImage *mentalImage,
                               Vector3 &desiredDirection,
                               float &desiredVelocity,
                               const Properties &teamTactics) {

  Vector3 myPos = player->GetPosition();
  Vector3 myMov = player->GetMovement();

  float offenseFactor = 0.7f + teamTactics.GetReal("dribble_offensiveness", 0.5f) * 0.05f + football::ai::GetMindSet(player->GetDynamicFormationEntry().role) * 0.05f;
  float powerMultiplier = 1.0f; // should alter (average) resulting velocity

  float future_sec = 0.25f;

  PlayerImage thisPlayerImage = mentalImage->GetPlayerImage(player);

  Team *team = player->GetTeam();
  signed int side = team->GetDynamicSide();

  std::vector<PlayerImage> opponentPlayerImages;

  std::vector<Player*> opponents;
  football::sim::query::GetClosestPlayers(match->GetTeam(std::abs(team->GetID() - 1)), myPos, false, opponents, 5);
  for (unsigned int i = 0; i < opponents.size(); i++) {
    opponentPlayerImages.push_back(mentalImage->GetPlayerImage(opponents[i]));
  }

  float nearBackline = NormalizedClamp(std::fabs(player->GetPosition().coords[0]) / pitchHalfW, 0.0f, 1.0f);
  float centerModifierInv =
      1.0f -
      std::pow(nearBackline,
               2.0f);  // near the end of the pitch, we want to get inside again
  centerModifierInv *= 0.5f; // stop going to the sides! wtf, todo, why does it prefer the sideline so much (probably because of no opponents :P)
  Vector3 oppGoalPos = Vector3(-side * pitchHalfW, myPos.coords[1] * (1.0f - teamTactics.GetReal("dribble_centermagnet", 0.5f)) * centerModifierInv, 0);


  std::vector<ForceSpot> forceField;

  for (unsigned int i = 0; i < opponentPlayerImages.size(); i++) {

    const PlayerImage &oppImg = opponentPlayerImages[i];

    ForceSpot spot;
    Vector3 oppPos = oppImg.position + oppImg.movement * future_sec;
    spot.origin = oppPos;
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 2.0f * powerMultiplier;//1.0f;
    spot.scale = 10.0f;//16.0f;
    spot.exp = 1.0f;//0.7f;
    forceField.push_back(spot);
  }

  // sideline / backline
  {
    ForceSpot spot;
    spot.origin = Vector3(myPos.coords[0], (pitchHalfH + 5.0f) * signSide(myPos.coords[1]), 0);
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 4.0f * powerMultiplier;
    spot.scale = 20.0f;
    spot.exp = 0.7f;
    forceField.push_back(spot);

    spot.origin = Vector3((pitchHalfW + 5.0f) * signSide(myPos.coords[0]), myPos.coords[1], 0);
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 4.0f * powerMultiplier;
    spot.scale = 20.0f;
    spot.exp = 0.7f;
    forceField.push_back(spot);
  }

  // love for da goal
  {
    ForceSpot spot;
    spot.origin = oppGoalPos;
    spot.magnetType = e_MagnetType_Attract;
    spot.decayType = e_DecayType_Constant;
    spot.power = offenseFactor * powerMultiplier;
    forceField.push_back(spot);
  }

  Vector3 forceFieldMovement = football::ai::GetForceFieldMovement(forceField, myPos + myMov * future_sec, 1.0f);

  desiredDirection = forceFieldMovement.GetNormalized(player->GetDirectionVec());
  desiredVelocity = clamp(forceFieldMovement.GetLength() * distanceToVelocityMultiplier, idleVelocity, sprintVelocity);
  desiredVelocity = RangeVelocity(desiredVelocity);
}

}  // namespace football::ai

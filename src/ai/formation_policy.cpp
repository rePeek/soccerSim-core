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


#include "ai/formation_policy.hpp"

#include <cmath>

#include "sim/match.hpp"
#include "sim/team.hpp"
#include "sim/player/player.hpp"

namespace football::ai {

Vector3 GetAdaptedFormationPosition(
    Match *match, Player *player, float backXBound, float frontXBound,
    float lowYBound, float highYBound, float xFocus, float xFocusStrength,
    float yFocus, float yFocusStrength, const Vector3 &microFocus,
    float microFocusStrength, float midfieldFocus, float midfieldFocusStrength,
    bool useDynamicFormationPosition) {
  Team *team = player->GetTeam();
  int side = team->GetDynamicSide();

  Vector3 position;

  if (useDynamicFormationPosition) {
    position = player->GetDynamicFormationEntry().position;
  } else {
    position = player->GetFormationEntry().position;
  }

  // stretch midfield into defending or attack position
  if (midfieldFocusStrength > 0.0f) {
    float midfieldPositionFactor = midfieldFocus * 2.0f - 1.0f; // -1 .. 1

    // only for midfielders
    float stretchBias = clamp(1.0f - std::fabs(position.coords[0] * 1.2f), 0.0f, 1.0f); // overstretch a bit so defenders/attackers are left alone (they usually aren't fully x = 0 or 1)
    stretchBias = curve(stretchBias, 1.0f);

    stretchBias *= midfieldFocusStrength;
    //if (player->GetTeam()->GetID() == 0) printf("==> %f\n", position.coords[0]);
    position.coords[0] = position.coords[0] * (1.0f - stretchBias) + midfieldPositionFactor * stretchBias;
    //if (player->GetTeam()->GetID() == 0) printf("--> %f %f %f\n", position.coords[0], midfieldPositionFactor, stretchBias);
  }

  float xLength = frontXBound - backXBound;
  position.coords[0] = backXBound + (position.coords[0] * 0.5f + 0.5f) * xLength;
  float yLength = highYBound - lowYBound;
  position.coords[1] = lowYBound + (position.coords[1] * -side * 0.5f + 0.5f) * yLength;

  Vector3 purePosition = position;

  if (xFocusStrength > 0.0f) {
    float bias = 1.0f - clamp( std::fabs(xFocus - position.coords[0]) / std::fabs(backXBound - frontXBound) , 0.0f, 1.0f);
    bias = -std::cos(bias * pi) * 0.5f + 0.5f;
    bias = std::pow(bias, 0.8);
    bias *= xFocusStrength;
    position.coords[0] = position.coords[0] * (1.0f - bias) + xFocus * bias;
  }

  if (yFocusStrength > 0.0f) {
    float distance = clamp( std::fabs(yFocus - position.coords[1]) / std::fabs(highYBound - lowYBound) , 0.0f, 1.0f);

    float bias = 1.0f - distance;
    bias *= 0.2f + 0.8f * std::fabs(yFocus) / pitchHalfH;
    bias *= yFocusStrength;

    position.coords[1] = position.coords[1] * (1.0f - bias) + yFocus * bias;
  }

  // microfocus
  if (microFocusStrength > 0.0f) {

    float homogeneousYInfluenceBias = 0.2f; // 1.0f == act as if everybody is on the same Y position (so, as if we're in 1D, with only X)
    float homogeneousYPositionBias = 0.4f; // 1.0f == don't influence player's resulting Y position (so, as if we're in 1D, with only X)

    // this way, players are more strictly keeping to their positions
    float distStatic = ((microFocus - purePosition) * Vector3(1, 1.0f - homogeneousYInfluenceBias, 0)).GetLength() / 50.0f;//26.0f;//32.0f;
    // this way, microfocus gets more direct attention from players around the action - they kinda 'forget' their base positions a bit though
    //float distDynamic = (microFocus - player->GetPosition()).GetLength() / 34.0f;

    float dist = distStatic;// * 0.3f + distDynamic * 0.7f;
    //dist = std::min(dist, 0.7f); // always some microfocus (exp)

    if (dist < 1.0f) {
      /*
      // wolfram alpha: (std::sin((x + 0.5) * pi) * 0.5 + 0.5) * 0.7 + (std::sin((x - 0.25) * 2.0 * pi) * 0.5 + 0.5) * 0.3 | from x = 0 to 1
      float microFocusBias1 = std::sin((dist + 0.5f) * pi) * 0.5f + 0.5f; // -\_
      float microFocusBias2 = std::sin((dist - 0.25f) * 2.0f * pi) * 0.5f + 0.5f; // _/-\_
      // mates in team in possession want to keep some distance for easier pass-to-ability, while defenders just want to jump into the action more

      float sineBias = 0.15f;// + clamp(player->GetTeam()->GetFadingTeamPossessionAmount() - 0.5f, 0.0f, 1.0f) * 0.2f;
      float microFocusBias = microFocusBias1 * (1.0f - sineBias) +
                             microFocusBias2 * sineBias;
      */

      float microFocusBias = 1.0f;

      // less serious when far away
      // we need this, because we use bias to vary between pos and microfocuspos. so for far away players, microfocuspos has way more influence since it's farther away.
      microFocusBias *= 1.0f - dist;

      // more of a binary choice to come over completely or not at all
      microFocusBias = curve(microFocusBias, 0.3f);

      // more bulgy curve
      //microFocusBias = std::pow(microFocusBias, 0.9f);

      // extra short distance peak
      float peakLocation = 0.15f;
      float peakWidth = 0.25f;
      float peakHeight = 0.1f;
      microFocusBias += (1.0f - NormalizedClamp(std::fabs(dist - peakLocation), 0.0f, peakWidth)) * peakHeight;
      microFocusBias = clamp(microFocusBias, 0.0f, 1.0f);

      // 'compressor' (wolfram alpha: x, (x^0.7) * 0.7 | from x = 0 to 1)
      // microFocusBias = std::pow(microFocusBias, 0.7f);
      // microFocusBias = microFocusBias * 0.7f;

      //printf("%f\n", microFocusStrength);
      microFocusBias *= microFocusStrength;
      //microFocusBias = clamp(microFocusBias, 0.0f, 0.1f);

      Vector3 microFocusPosition = microFocus * Vector3(1, 1.0f - homogeneousYPositionBias, 1) + position * Vector3(0, homogeneousYPositionBias, 0);
      position = position * (1.0f - microFocusBias) + microFocusPosition * (microFocusBias);
    }
  }

  return position;
}

float GetMindSet(e_PlayerRole role) {
  float mindSet = 0.5;

  if (role == e_PlayerRole_GK) mindSet = 0.0;

  if (role == e_PlayerRole_CB) mindSet = 0.0;

  if (role == e_PlayerRole_LB ||
      role == e_PlayerRole_RB) mindSet = 0.25;

  if (role == e_PlayerRole_DM) mindSet = 0.25;

  if (role == e_PlayerRole_LM ||
      role == e_PlayerRole_CM ||
      role == e_PlayerRole_RM) mindSet = 0.5;

  if (role == e_PlayerRole_AM) mindSet = 0.75;

  if (role == e_PlayerRole_CF) mindSet = 1.0;

  return mindSet;
}

}  // namespace football::ai

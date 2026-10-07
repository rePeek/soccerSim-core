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


#include "sim/query/player_query.hpp"

#include <cmath>
#include <cassert>
#include <map>

#include "sim/observation/mentalimage.hpp"
#include "sim/match/match.hpp"
#include "sim/team/team.hpp"
#include "sim/player/player.hpp"
#include "sim/ball/ball.hpp"

namespace football::sim::query {

float CalculateFreeSpace(Match *match, const MentalImage *mentalImage,
                            int teamID, const Vector3 &focusPos,
                            float safeDistance, float futureTime_sec) {


  assert(mentalImage);

  float currentSituation = 0.0f;

  auto opponentPlayerImages = mentalImage->GetTeamPlayerImages(std::abs(teamID - 1), match->GetTimelineTick());

  // player position predictions
  for (int i = 0; i < (signed int)opponentPlayerImages.size(); i++) {
    if (opponentPlayerImages[i].player_role != e_PlayerRole_GK) {

      // resulting opp position
      opponentPlayerImages[i].position = opponentPlayerImages[i].position + opponentPlayerImages[i].movement * 0.2f; // slowness
      Vector3 toFocusMovement = (focusPos - opponentPlayerImages[i].position).GetNormalized(0) * sprintVelocity * clamp(futureTime_sec - 0.2f, 0.0f, 1000.0f);
      if (toFocusMovement.GetLength() > (focusPos - opponentPlayerImages[i].position).GetLength()) toFocusMovement = focusPos - opponentPlayerImages[i].position;
      opponentPlayerImages[i].position += toFocusMovement;

      float situation = 1.0f - clamp((opponentPlayerImages[i].position - focusPos).GetLength(), 0, safeDistance) / safeDistance;

      currentSituation += situation;
    }
  }

  return 1.0f - NormalizedClamp(currentSituation, 0.0f, 2.5f);
}

bool HasPossession(Ball *ball, Player *player) {
  Vector3 playerMovement = player->GetMovement();

  // premature optimization ;)
  if ((player->GetPosition() - ball->Predict(0)).GetLength() > 5.0) return false;

  Vector3 ballMovement = ball->GetMovement();
  if (std::fabs(ball->Predict(0).coords[2]) > 0.5) return false;


  bool distanceOK = true;
  float radius = 1.0f;
  Vector3 center = player->GetPosition() + player->GetMovement() * 0.05f + player->GetDirectionVec() * 0.1f;//was: 0.05f
  if ((ball->Predict(0).Get2D() - center).GetLength() > radius) distanceOK = false;

  bool movementOK = true;
  Vector3 ballMovement3D = (ball->Predict(10) - ball->Predict(0)) * 100.0f;
  if ((ballMovement3D - playerMovement).GetLength() > 6.0f) movementOK = false;

  if (distanceOK && movementOK) return true; else return false;
}

Player *GetClosestPlayer(Team *team, const Vector3 &position, Player *except) {
  float closestDistance = 10000;
  Player *closestPlayer = nullptr;
  for (Player *player : team->GetAllPlayers()) {
    if (!player->IsActive() || player == except) continue;
    const float distance = (player->GetPosition() - position).GetLength();
    if (distance < closestDistance) {
      closestDistance = distance;
      closestPlayer = player;
    }
  }
  return closestPlayer;
}

void GetClosestPlayers(Team *team, const Vector3 &position,
                       std::vector<Player *> &result, unsigned int playerCount) {
  // Equivalent keys preserve insertion/roster order, never PlayerId order.
  std::multimap<float, Player *> sorted;
  for (Player *player : team->GetAllPlayers()) {
    if (player->IsActive())
      sorted.emplace((player->GetPosition() - position).GetLength(), player);
  }
  auto iter = sorted.begin();
  for (unsigned int i = 0; i < playerCount && iter != sorted.end(); ++i, ++iter)
    result.push_back(iter->second);
}

}  // namespace football::sim::query

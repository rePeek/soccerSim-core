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

#include "sim/ai_support/mentalimage.hpp"
#include "sim/match.hpp"
#include "sim/team.hpp"
#include "sim/player/player.hpp"
#include "sim/ball.hpp"

namespace football::sim::query {

float CalculateFreeSpace(Match *match, const MentalImage *mentalImage,
                            int teamID, const Vector3 &focusPos,
                            float safeDistance, float futureTime_sec) {
  //void football::sim::query::GetClosestPlayers(Team *team, const Vector3 &position, bool onlyAIControlled, std::vector<Player*> &result, unsigned int playerCount)


  assert(mentalImage);

  float currentSituation = 0.0f;

  auto opponentPlayerImages = mentalImage->GetTeamPlayerImages(std::abs(teamID - 1));

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

Player *GetClosestPlayer(Team *team, const Vector3 &position,
                            bool onlyAIControlled, Player *except,
                            bool onlySelectable) {
  const std::vector<Player*> &players = team->GetAllPlayers();

  float closestDistance = 10000;
  Player *closestPlayer = 0;

  for (auto p : players) {
    if (p->IsActive() && p != except) {
      float distance = (p->GetPosition() - position).GetLength();
      if (distance < closestDistance) {
        if ((!onlyAIControlled || !p->ExternalControllerActive()) &&
            (!onlySelectable || p->GetFormationEntry().controllable)) {
          closestDistance = distance;
          closestPlayer = p;
        }
      }
    }
  }

  return closestPlayer;
}

void GetClosestPlayers(Team *team, const Vector3 &position,
                          bool onlyAIControlled, std::vector<Player *> &result,
                          unsigned int playerCount, bool onlySelectable) {
  const std::vector<Player*> &players = team->GetAllPlayers();
  std::multimap<float, Player*> tmpResult;

  //printf("total players: %i\n", players.size());

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      float distance = (players[i]->GetPosition() - position).GetLength();
        if ((!onlyAIControlled || !players[i]->ExternalControllerActive()) &&
            (!onlySelectable || players[i]->GetFormationEntry().controllable)) {
        tmpResult.insert(std::pair<float, Player*>(distance, players[i]));
      }
    }
  }

  //printf("tmp players: %i\n", tmpResult.size());

  std::map<float, Player*>::iterator iter = tmpResult.begin();
  for (unsigned int i = 0; i < playerCount && iter != tmpResult.end(); i++) {
    result.push_back(iter->second);
    iter++;
  }

  //printf("result: %i\n", result.size());
}

Player *GetBestSwitchTargetPlayer(Match *match, Team *team,
                                     const Vector3 &desiredMovement) {

  // find most interesting position on pitch

  Vector3 actionPosition = match->GetDesignatedPossessionPlayer()->GetPosition() * 0.5f +
                           match->GetBall()->Predict(100).Get2D() * 0.5f;

  Vector3 defensePosition = (actionPosition * Vector3(1.0f, 0.8f, 0.0f)) +
                            Vector3(team->GetDynamicSide() * 4.0f, 0.0f, 0.0f);
  Vector3 offensePosition = (actionPosition * Vector3(1.0f, 0.8f, 0.0f)) +
                            Vector3(-team->GetDynamicSide() * 8.0f, 0.0f, 0.0f);

  // experiment: also take team possession player into account, try to pick one near
  defensePosition = defensePosition * 0.8f + team->GetDesignatedTeamPossessionPlayer()->GetPosition() * 0.2f;

  float offenseBias = team->GetFadingTeamPossessionAmount() - 0.5f;
  offenseBias = offenseBias * 0.2f + clamp(team->GetTeamPossessionAmount() - 0.5f, 0.0f, 1.0f) * 0.8f; // more direct, more urgent
  offenseBias = clamp((offenseBias - 0.5f) * 2.0 + 0.5f, 0.0f, 1.0f); // make more binary
  offenseBias =
      clamp(std::pow(offenseBias, 1.5f), 0.0f, 1.0f);  // tend towards defensive

  Vector3 resultingPosition = defensePosition * (1.0f - offenseBias) + offensePosition * offenseBias;

  assert(offenseBias >= 0.0f && offenseBias <= 1.0f);

  // get sorted list of closest players

  std::vector<Player*> teamPlayers;
  football::sim::query::GetClosestPlayers(team, resultingPosition, true, teamPlayers, 8);
  std::vector<Player*>::iterator iter = teamPlayers.begin();
  while (iter != teamPlayers.end()) {
    if ((*iter)->GetFormationEntry().role == e_PlayerRole_GK) {
      iter = teamPlayers.erase(iter);
    } else {
      iter++;
    }
  }

  if (teamPlayers.size() == 0) return 0;


  // in case of defending, we ideally need someone who is closer to our goal than the opponent

  int bestPlayerIndex = 0; // closest player - sorted first in teamplayers array
  float tooLateDistance = 1.0f;

  Player *designated = match->GetDesignatedPossessionPlayer();
  if (designated->GetTeam() != team) {
    Player *opp = designated;
    Vector3 goalPos = Vector3(team->GetDynamicSide() * pitchHalfW, 0.0f, 0.0f);
    float oppGoalDist = (goalPos - (opp->GetPosition() + opp->GetMovement() * 0.5f)).GetLength();
    for (unsigned int i = 0; i < teamPlayers.size(); i++) {
      float mateGoalDist = (goalPos - (teamPlayers[i]->GetPosition() + teamPlayers[i]->GetMovement() * 0.5f)).GetLength();
      if (mateGoalDist < oppGoalDist + tooLateDistance +
                             clamp(oppGoalDist * 0.1f, 0.0f, 3.0f)) {
          // doesn't matter much when opp is still far away from
                        // goal
        bestPlayerIndex = i;
        break;
      }
    }
  }

  return teamPlayers.at(bestPlayerIndex);
}

}  // namespace football::sim::query

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
// this work is public domain. the code is undocumented, scruffy, untested, and
// should generally not be used for anything important. i do not offer support,
// so don't ask. to be used for inspiration :)

#include <algorithm>
#include "sim/team.hpp"

#include "sim/gamedefines.hpp"
#include "env/main.hpp"
#include "sim/utils.hpp"
#include "sim/ai_support/AIfunctions.hpp"
#include "sim/match.hpp"

Team::Team(int id, Match *match, TeamData *teamData, float aiDifficulty)
    : id(id), match(match), teamData(teamData), aiDifficulty(aiDifficulty) {
  assert(id == 0 || id == 1);
  // assert(teamData->GetPlayerNum() >= playerNum); // does team have enough
  // players?

  teamController = new TeamAIController(this);

  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0;
  fadingTeamPossessionAmount = 1.0;
}

Team::~Team() {}

void Team::Mirror() {
  side *= -1;
  mirrored = !mirrored;
  for (auto &p : players) {
    p->Mirror();
  }
}

void Team::Exit() {
  Hide2D();

  humanGamers.clear();
  for (unsigned int i = 0; i < players.size(); i++) {
    delete players[i];
  }

  delete teamController;

}

void Team::InitPlayers() {
  // load all players in the team, even the players who sit on the bench. aww.
  for (int i = 0; i < (signed int)teamData->GetPlayerNum(); i++) {
    PlayerData *playerData = teamData->GetPlayerData(i);
    Player *player = new Player(this, playerData);
    players.push_back(player);

    if (i < playerNum) {
      // activate playerCount players (the starting eleven, usually)
      auto formation = GetFormationEntry(player);
      player->Activate(formation.lazy);
    }
  }

  designatedTeamPossessionPlayer = players.at(0);
}

FormationEntry Team::GetFormationEntry(void *player) {
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players[i] == player) {
      return teamData->GetFormationEntry(i);
    }
  }

  assert(1 == 2);
  FormationEntry fail;
  return fail;
}

void Team::SetFormationEntry(Player *player, FormationEntry entry) {
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players[i] == player) {
      teamData->SetFormationEntry(i, entry);
    }
  }
}

void Team::GetActivePlayers(std::vector<Player *> &activePlayers) {
  for (auto player : players) {
    if (player->IsActive()) activePlayers.push_back(player);
  }
}

int Team::GetActivePlayersCount() const {
  int count = 0;
  for (auto player : players) {
    if (player->IsActive()) count++;
  }
  return count;
}

void Team::AddHumanGamers(const std::vector<AIControlledKeyboard*>& controllers) {
  for (auto controller : controllers) {
    humanGamers.push_back(std::make_unique<HumanGamer>(this, controller));
    switchPriority.push_back(humanGamers.size() - 1);
  }
  UpdateDesignatedTeamPossessionPlayer();
  std::vector<Player*> result;
  AI_GetClosestPlayers(this, match->GetBall()->Predict(0).Get2D(), true, result, controllers.size(), true);
  if (!result.empty()) {
    mainSelectedPlayer = result[0];
  }
  // Sort players by the ID to provide identity mapping of controller-player
  // when controlling all players on the team.
  std::sort(result.begin(), result.end(), [](Player* a, Player* b) {
        return a->GetStableID() < b->GetStableID();
    });
  for (unsigned int i = 0; i < result.size(); i++) {
    humanGamers[i]->SetSelectedPlayer(result[i]);
  }
}

void Team::UpdateDesignatedTeamPossessionPlayer() {
  designatedTeamPossessionPlayer =
      AI_GetClosestPlayer(this, match->GetBall()->Predict(0).Get2D(), false);
}

void Team::DeleteHumanGamers() {
  humanGamers.clear();
  switchPriority.clear();
}

e_PlayerColor Team::GetPlayerColor(PlayerBase *player) {
  if (player->ExternalController()) {
    return player->ExternalController()->GetHIDevice()->GetPlayerColor();
  }
  return e_PlayerColor_Default;
}

int Team::HumanControlledToBallDistance() {
  int timeToBall = 10000;
  for (auto& human : humanGamers) {
    if (human->GetSelectedPlayer() && !human->GetHIDevice()->Disabled()) {
      timeToBall =
          std::min(timeToBall,
                   human->GetSelectedPlayer()->GetTimeNeededToGetToBall_ms());
    }
  }
  return timeToBall;
}

bool Team::HasPossession() const { return hasPossession; }

bool Team::HasUniquePossession() const {
  return HasPossession() && !match->GetTeam(1 - id)->HasPossession();
}

int Team::GetTimeNeededToGetToBall_ms() const {
  return timeNeededToGetToBall_ms;
}

Player *Team::GetBestPossessionPlayer() {
  int bestTime_ms = 10000000;
  Player *bestPlayer = 0;
  for (auto p : players) {
    if (p->IsActive()) {
      int time_ms = p->GetTimeNeededToGetToBall_ms();
      if (time_ms < bestTime_ms) {
        bestTime_ms = time_ms;
        bestPlayer = p;
      }
    }
  }

  assert(bestPlayer);

  return bestPlayer;
}

float Team::GetTeamPossessionAmount() const { return teamPossessionAmount; }

float Team::GetFadingTeamPossessionAmount() const {
  return fadingTeamPossessionAmount;
}

void Team::SetFadingTeamPossessionAmount(float value) {
  fadingTeamPossessionAmount = clamp(value, 0.5, 1.5);
}

void Team::SetLastTouchPlayer(Player *player, e_TouchType touchType) {
  lastTouchPlayer = player;
  player->SetLastTouchTime_ms(match->GetActualTime_ms());
  player->SetLastTouchType(touchType);
  match->SetLastTouchTeamID(GetID(), touchType);
}

void Team::ResetSituation(const Vector3 &focusPos) {
  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;
  lastTouchPlayer = 0;

  designatedTeamPossessionPlayer = players.at(0);

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->ResetSituation(focusPos);
    }
  }

  GetController()->Reset();
}

void Team::HumanGamersSelectAnyone() {
  // make sure all human gamers have a player selected
  if (match->IsInPlay()) {
    if (mainSelectedPlayer == nullptr) {
      mainSelectedPlayer = AI_GetClosestPlayer(
          this, match->GetBall()->Predict(0).Get2D(), true, 0, true);
    }
    for (unsigned int i = 0; i < humanGamers.size(); i++) {
      if (!humanGamers[i]->GetSelectedPlayer()) {
        Player *player = AI_GetClosestPlayer(
            this, match->GetBall()->Predict(0).Get2D(), true, 0, true);
        if (player) {
          humanGamers[i]->SetSelectedPlayer(player);
        }
      }
    }
  }
}

void Team::SelectPlayer(Player *player) {
  if (player->GetFormationEntry().controllable) {
    mainSelectedPlayer = player;
    if (!player->ExternalController() && !humanGamers.empty()) {
        // already selected
      humanGamers.at(*switchPriority.begin())->SetSelectedPlayer(player);
      switchPriority.push_back(*switchPriority.begin());
      switchPriority.pop_front();
    }
  }
  designatedTeamPossessionPlayer = player;
}

void Team::DeselectPlayer(Player *player) {
  for (unsigned int i = 0; i < humanGamers.size(); i++) {
    Player* selectedPlayer = humanGamers[i]->GetSelectedPlayer();
    if (selectedPlayer == player) {
      Player *somePlayer =
          AI_GetClosestPlayer(this, player->GetPosition(), true, player, true);
      if (somePlayer) {
        mainSelectedPlayer = somePlayer;
        humanGamers[i]->SetSelectedPlayer(somePlayer);
      } else {
        mainSelectedPlayer = nullptr;
        humanGamers[i]->SetSelectedPlayer(0);
      }
    }
  }
}

void Team::RelaxFatigue(float howMuch) {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->RelaxFatigue(howMuch);
    }
  }
}

void Team::Process() {
  teamPossessionAmount = (float)(match->GetTeam(abs(GetID() - 1))
      ->GetTimeNeededToGetToBall_ms() +
      1500) /
      (float)(GetTimeNeededToGetToBall_ms() + 1500);
  float tmpFadingTeamPossessionAmount =
      fadingTeamPossessionAmount * 0.995f +
      clamp(teamPossessionAmount, 0.5f, 1.5f) * 0.005f;
  fadingTeamPossessionAmount +=
      clamp(tmpFadingTeamPossessionAmount - fadingTeamPossessionAmount,
            -0.005f, 0.005f);  // maximum change per 10ms

  if (!match->IsInPlay() || match->IsInSetPiece() ||
      match->GetBallRetainer() != 0) {
    if (match->GetBallRetainer() != 0) {
      fadingTeamPossessionAmount = teamPossessionAmount =
          (match->GetBallRetainer()->GetTeam() == this) ? 1.5f : 0.5f;
    } else {
      fadingTeamPossessionAmount = teamPossessionAmount =
          (match->GetBestPossessionTeam() == this) ? 1.5f : 0.5f;
    }
  }

  HumanGamersSelectAnyone();

  if (match->IsInPlay() && !match->IsInSetPiece()) {
    teamController->Process();

    int team_offset = id == match->SecondTeam() ? 200 : 0;
    if ((match->GetActualTime_ms() + team_offset) % 400 == 0) {
      teamController->CalculateDynamicRoles();
    }

    if ((match->GetActualTime_ms() + team_offset + 100) % 400 == 0) {
      teamController->CalculateManMarking();
    }
  }

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->Process();
    }
  }

  if (match->IsInPlay()) {
    for (unsigned int i = 0; i < humanGamers.size(); i++) {
      // switch button
      Player *selectedPlayer = humanGamers[i]->GetSelectedPlayer();
      if (humanGamers[i]->GetHIDevice()->GetButton(e_ButtonFunction_Switch) &&
          !humanGamers[i]->GetHIDevice()->GetPreviousButtonState(
              e_ButtonFunction_Switch) &&
              // don't switch if we are both best AND designated possession
              // player. unless opponent team has ball.
              (!(selectedPlayer == GetBestPossessionPlayer() &&
                  selectedPlayer == designatedTeamPossessionPlayer) ||
                  GetTeamPossessionAmount() < 1.0f) &&
                  !selectedPlayer->HasUniquePossession()) {
        Player *targetPlayer = 0;

        if (!designatedTeamPossessionPlayer->ExternalController() &&
            match->GetBestPossessionTeam() == this) {
          targetPlayer = designatedTeamPossessionPlayer;
        } else if (!GetBestPossessionPlayer()->ExternalController() &&
            match->GetBestPossessionTeam() == this) {
          targetPlayer = GetBestPossessionPlayer();
        } else {
          targetPlayer = AI_GetBestSwitchTargetPlayer(
              match, this, humanGamers[i]->GetHIDevice()->GetDirection());
          if (targetPlayer)
            if (targetPlayer->ExternalController()) targetPlayer = 0;
        }
        if (targetPlayer == GetGoalie()) {
          // can not be goalie in current version, at least not during
          // play, unless being directly passed to by teammate
          targetPlayer = 0;
        }
        if (targetPlayer && targetPlayer->GetFormationEntry().controllable) {
          mainSelectedPlayer = targetPlayer;
          humanGamers[i]->SetSelectedPlayer(targetPlayer);
        }
      }
    }

  } else {
    // make sure all human gamers don't have a player selected.
    // Don't do this for the very first step, as otherwise agent gets no player
    // controlled in the observations returned by reset().
    if (match->GetActualTime_ms() >= 2000) {
      if (GetScenarioConfig().DynamicPlayerSelection()) {
        for (unsigned int i = 0; i < humanGamers.size(); i++) {
          if (humanGamers[i]->GetSelectedPlayer()) {
            humanGamers[i]->SetSelectedPlayer(0);
          }
        }
      }
      mainSelectedPlayer = nullptr;
    }
  }

  int designatedPlayerTime_ms =
      designatedTeamPossessionPlayer->GetTimeNeededToGetToBall_ms();
  Player *bestPlayer = GetBestPossessionPlayer();
  int oppTime_ms =
      match->GetTeam(abs(GetID() - 1))->GetTimeNeededToGetToBall_ms();
  if (designatedTeamPossessionPlayer != bestPlayer) {
    // switch only if other player is somewhat better, to overcome
    // possession-chaos
    int bestPlayerTime_ms = bestPlayer->GetTimeNeededToGetToBall_ms();
    float timeRating = (float)(bestPlayerTime_ms + 500) /
        (float)(designatedPlayerTime_ms + 500);

    if (bestPlayer->HasPossession()) timeRating *= 0.5f;
    if (designatedTeamPossessionPlayer->HasPossession()) timeRating /= 0.5f;

    if (bestPlayer->ExternalControllerActive()) timeRating *= 0.8f;
    if (designatedTeamPossessionPlayer->ExternalControllerActive())
      timeRating /= 0.8f;

    // current player can get to the ball before the closest opponent: less
    // need to switch
    // if (GetID() == 0) printf("opptime: %i, designated time: %i\n",
    // oppTime_ms, designatedPlayerTime_ms);
    if (!bestPlayer->ExternalControllerActive() &&
        designatedPlayerTime_ms < oppTime_ms - 100) {
      timeRating += 0.2f;
      timeRating *= 1.2f;
    }

    if (timeRating < 0.8f) {
      designatedTeamPossessionPlayer = bestPlayer;
    }
  }
}

void Team::Put2D(bool mirror) {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->Put2D(mirror);
    }
  }
}

void Team::Hide2D() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->Hide2D();
    }
  }
}

void Team::UpdatePossessionStats() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->UpdatePossessionStats();
    }
  }

  // possession?

  hasPossession = false;
  timeNeededToGetToBall_ms = 100000;
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players[i]->IsActive()) {
      if (players[i]->HasPossession()) hasPossession = true;
      if (players[i]->GetTimeNeededToGetToBall_ms() < timeNeededToGetToBall_ms)
        timeNeededToGetToBall_ms = players[i]->GetTimeNeededToGetToBall_ms();
    }
  }
}

void Team::UpdateSwitch() {
  // lose turn on ball possession

  if (match->IsInPlay() && humanGamers.size() > 1) {
    int myTurn = *switchPriority.begin();
    if (humanGamers.at(myTurn)->GetSelectedPlayer() ==
        match->GetDesignatedPossessionPlayer()) {
      switchPriority.pop_front();
      switchPriority.push_back(myTurn);
    }
  }

  // autoswitch on proximity

  if (match->IsInPlay() && !humanGamers.empty()) {
    if (!designatedTeamPossessionPlayer->ExternalControllerActive() &&
        3 * designatedTeamPossessionPlayer->GetTimeNeededToGetToBall_ms() <
            HumanControlledToBallDistance() &&
        designatedTeamPossessionPlayer->GetFormationEntry().role !=
            e_PlayerRole_GK) {
      SelectPlayer(designatedTeamPossessionPlayer);
    }
  }

  // team player in possession is not human selected

  if (match->IsInPlay() && !humanGamers.empty()) {
    if (!designatedTeamPossessionPlayer->ExternalControllerActive() &&
        (designatedTeamPossessionPlayer->HasUniquePossession() ||
         match->IsInSetPiece())) {
      if (designatedTeamPossessionPlayer->GetFormationEntry().role !=
          e_PlayerRole_GK) {
        SelectPlayer(designatedTeamPossessionPlayer);
      }
    }
  }
}

Player *Team::GetGoalie() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      if (players[i]->GetFormationEntry().role == e_PlayerRole_GK)
        return players[i];
    }
  }

  return 0;
}

void Team::ProcessState(EnvState *state) {
  state->process(hasPossession);
  state->process(timeNeededToGetToBall_ms);
  state->process(designatedTeamPossessionPlayer);
  state->process(teamPossessionAmount);
  state->process(fadingTeamPossessionAmount);
  teamController->ProcessState(state);
  int size = humanGamers.size();
  state->process(size);
  humanGamers.resize(size);
  for (auto &g : humanGamers) {
    g->ProcessState(state);
  }
  state->process(switchPriority);
  state->process(lastTouchPlayer);
  state->process(mainSelectedPlayer);
}

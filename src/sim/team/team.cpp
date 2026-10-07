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
#include "sim/team/team.hpp"

#include "sim/query/player_query.hpp"
#include "sim/match/match.hpp"
#include "sim/team/formation.hpp"
#include "sim/rules/offside.hpp"

Team::Team(int id, Match *match, const football::model::Team& model,
           float aiDifficulty)
    : id(id), match(match), model_(model), formation_(BuildFormation(model)),
      aiDifficulty(aiDifficulty), static_side_(id == 0 ? -1 : 1) {
  assert(id == 0 || id == 1);
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

void Team::SwitchEnds() {
  static_side_ = -static_side_;
  side = -side;
  for (auto &p : players) {
    p->Mirror();
  }
}

void Team::Exit() {
  Hide2D();

  for (unsigned int i = 0; i < players.size(); i++) {
    delete players[i];
  }


}

void Team::InitPlayers(std::uint8_t first_schedule_phase, const AnimationLibrary& animations) {
  // Roster traversal supplies order; phases repeat every ten players.
  std::uint8_t schedule_phase = first_schedule_phase;
  for (std::size_t i = 0; i < formation_.size(); ++i) {
    Player *player = new Player(this, model_.players[i], schedule_phase, animations);
    schedule_phase = (schedule_phase + 1) % 10;
    players.push_back(player);

    if (i < playerNum) {
      // activate playerCount players (the starting eleven, usually)
      player->Activate();
    }
  }

  designatedTeamPossessionPlayer = players.at(0);
}

FormationEntry Team::GetFormationEntry(void *player) {
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players[i] == player) {
      return formation_.at(i);
    }
  }

  assert(1 == 2);
  FormationEntry fail;
  return fail;
}

void Team::SetFormationEntry(Player *player, FormationEntry entry) {
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players[i] == player) {
      formation_.at(i) = entry;
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

void Team::UpdateDesignatedTeamPossessionPlayer() {
  designatedTeamPossessionPlayer =
      football::sim::query::GetClosestPlayer(this, match->GetBall()->Predict(0).Get2D());
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


void Team::ResetSituation(const Vector3 &focusPos) {
  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;

  designatedTeamPossessionPlayer = players.at(0);

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->ResetSituation(focusPos);
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

void Team::Process(std::span<MentalImage> history, football::sim::BallTouchSink& touch_sink) {
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

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      players[i]->Process(history, touch_sink);
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

    // current player can get to the ball before the closest opponent: less
    // need to switch
    // if (GetID() == 0) printf("opptime: %i, designated time: %i\n",
    // oppTime_ms, designatedPlayerTime_ms);
    if (designatedPlayerTime_ms < oppTime_ms - 100) {
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


Player *Team::GetGoalie() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players[i]->IsActive()) {
      if (players[i]->GetFormationEntry().role == e_PlayerRole_GK)
        return players[i];
    }
  }

  return 0;
}

Player *Team::GetPieceTaker() {
  const auto &restart = match->GetReferee()->GetBuffer();
  return restart.active && restart.teamID == id ? restart.taker : nullptr;
}

e_GameMode Team::GetSetPieceType() {
  const auto &restart = match->GetReferee()->GetBuffer();
  return restart.active ? restart.desiredSetPiece : e_GameMode_Normal;
}

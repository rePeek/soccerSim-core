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

#include "sim/team/formation.hpp"

Team::Team(int id, const football::model::Team& model,
           float aiDifficulty)
    : id(id), model_(model), formation_(BuildFormation(model)),
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
  for (Player* p : player_views_) {
    p->Mirror();
  }
}

void Team::SwitchEnds() {
  static_side_ = -static_side_;
  side = -side;
  for (Player* p : player_views_) {
    p->Mirror();
  }
}

void Team::Exit(football::sim::Tick now) {
  for (Player* player : player_views_) {
    player->Exit(now);
  }
  players_.clear();
  player_views_.clear();
}

void Team::InitPlayers(std::uint8_t first_schedule_phase, const AnimationLibrary& animations, const football::model::Pitch& pitch, blunted::Rng& rng) {
  // Roster traversal supplies order; phases repeat every ten players.
  std::uint8_t schedule_phase = first_schedule_phase;
  for (std::size_t i = 0; i < formation_.size(); ++i) {
    auto player = std::make_unique<Player>(this, model_.players[i], schedule_phase, animations, pitch, rng);
    schedule_phase = (schedule_phase + 1) % 10;
    player_views_.push_back(player.get());
    players_.push_back(std::move(player));

    if (i < playerNum) {
      // activate playerCount players (the starting eleven, usually)
      player_views_.back()->Activate();
    }
  }

  designatedTeamPossessionPlayer = player_views_.at(0);
}

FormationEntry Team::GetFormationEntry(void *player) {
  for (int i = 0; i < (signed int)player_views_.size(); i++) {
    if (player_views_[i] == player) {
      return formation_.at(i);
    }
  }

  assert(1 == 2);
  FormationEntry fail;
  return fail;
}

void Team::SetFormationEntry(Player *player, FormationEntry entry) {
  for (int i = 0; i < (signed int)player_views_.size(); i++) {
    if (player_views_[i] == player) {
      formation_.at(i) = entry;
    }
  }
}

void Team::GetActivePlayers(std::vector<Player *> &activePlayers) {
  for (Player* player : player_views_) {
    if (player->IsActive()) activePlayers.push_back(player);
  }
}

int Team::GetActivePlayersCount() const {
  int count = 0;
  for (Player* player : player_views_) {
    if (player->IsActive()) count++;
  }
  return count;
}

bool Team::HasPossession() const { return hasPossession; }


int Team::GetTimeNeededToGetToBall_ms() const {
  return timeNeededToGetToBall_ms;
}

Player *Team::GetBestPossessionPlayer() {
  int bestTime_ms = 10000000;
  Player *bestPlayer = 0;
  for (Player* p : player_views_) {
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


void Team::ResetSituation(const Vector3 &focusPos, football::sim::Tick now) {
  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;

  designatedTeamPossessionPlayer = player_views_.at(0);

  for (unsigned int i = 0; i < player_views_.size(); i++) {
    if (player_views_[i]->IsActive()) {
      player_views_[i]->ResetSituation(focusPos, now);
    }
  }

}


void Team::RelaxFatigue(float howMuch) {
  for (unsigned int i = 0; i < player_views_.size(); i++) {
    if (player_views_[i]->IsActive()) {
      player_views_[i]->RelaxFatigue(howMuch);
    }
  }
}




Player *Team::GetGoalie() {
  for (unsigned int i = 0; i < player_views_.size(); i++) {
    if (player_views_[i]->IsActive()) {
      if (player_views_[i]->GetFormationEntry().role == e_PlayerRole_GK)
        return player_views_[i];
    }
  }

  return 0;
}

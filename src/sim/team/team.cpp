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
#include "football/pitch/pitch.hpp"
#include "sim/team/team.hpp"


Team::Team(int id, football::sim::Roster& roster, football::sim::TeamFrame& frame,
           football::sim::TeamMatchState& state)
    : id(id), roster_(roster), frame_(frame), state_(state) {
  assert(id == 0 || id == 1);
}

Team::~Team() {}

void Team::Mirror() {
  frame_.dynamic_side *= -1;
  frame_.mirrored = !frame_.mirrored;
  for (Player* p : roster_.Players()) {
    p->Mirror();
    p->SetTeamContext(TeamFacts());
  }
}

void Team::SwitchEnds() {
  frame_.static_side = -frame_.static_side;
  frame_.dynamic_side = -frame_.dynamic_side;
  for (Player* p : roster_.Players()) {
    p->Mirror();
    p->SetTeamContext(TeamFacts());
  }
}



void Team::GetActivePlayers(std::vector<Player *> &activePlayers) {
  for (Player* player : roster_.Players()) {
    if (player->IsActive()) activePlayers.push_back(player);
  }
}

int Team::GetActivePlayersCount() const {
  int count = 0;
  for (Player* player : roster_.Players()) {
    if (player->IsActive()) count++;
  }
  return count;
}

bool Team::HasPossession() const { return state_.has_possession; }


int Team::GetTimeNeededToGetToBall_ms() const {
  return state_.time_to_ball_ms;
}

Player *Team::GetBestPossessionPlayer() {
  int bestTime_ms = 10000000;
  Player *bestPlayer = 0;
  for (Player* p : roster_.Players()) {
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

float Team::GetTeamPossessionAmount() const { return state_.possession_amount; }

float Team::GetFadingTeamPossessionAmount() const {
  return state_.fading_possession_amount;
}

void Team::SetFadingTeamPossessionAmount(float value) {
  state_.fading_possession_amount = clamp(value, 0.5, 1.5);
  PublishTeamFacts();
}


void Team::ResetSituation(const Vector3 &focusPos, football::sim::Tick now) {
  state_.time_to_ball_ms = 100;
  state_.has_possession = false;

  state_.possession_amount = 1.0f;
  state_.fading_possession_amount = 1.0f;

  state_.designated_player = roster_.Players().front();

  for (Player* player : roster_.Players()) {
    if (player->IsActive()) {
      player->ResetSituation(focusPos, now);
    }
    player->SetTeamContext(TeamFacts());
  }

}


void Team::RelaxFatigue(float howMuch) {
  for (unsigned int i = 0; i < roster_.Players().size(); i++) {
    if (roster_.Players()[i]->IsActive()) {
      roster_.Players()[i]->RelaxFatigue(howMuch);
    }
  }
}




Player *Team::GetGoalie() const { return roster_.Goalie(); }

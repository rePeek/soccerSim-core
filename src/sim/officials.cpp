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
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "sim/officials.hpp"


#include "sim/player/playerofficial.hpp"
#include "sim/player/humanoid/humanoidbase.hpp"

#include "data/playerdata.hpp"

#include "env/main.hpp"

Officials::Officials(Match *match)
    : match(match) {
  DO_VALIDATION;
  playerData = new PlayerData();
  referee = new PlayerOfficial(e_OfficialType_Referee, match, playerData);
  linesmen[0] = new PlayerOfficial(e_OfficialType_Linesman, match, playerData);
  linesmen[1] = new PlayerOfficial(e_OfficialType_Linesman, match, playerData);

  referee->Activate(false);
  linesmen[0]->Activate(false);
  linesmen[1]->Activate(false);

  // Route official placement through PlayerBase::ResetPosition so the
  // simulation-owned movement state is synchronized. Calling
  // CastHumanoid()->ResetPosition() directly would leave PlayerKinematicState
  // stale (a 'half-tick' actor: humanoid moved, authoritative state not).
  referee->ResetPosition(Vector3(10, -10, 0), Vector3(0));
  linesmen[0]->ResetPosition(Vector3(25, -36.5, 0), Vector3(0));
  linesmen[1]->ResetPosition(Vector3(-25, 36.5, 0), Vector3(0));
}

Officials::~Officials() {
  DO_VALIDATION;
  delete referee;
  delete linesmen[0];
  delete linesmen[1];
  delete playerData;
}

void Officials::Mirror() {
  DO_VALIDATION;
  referee->Mirror();
  linesmen[0]->Mirror();
  linesmen[1]->Mirror();
}

void Officials::GetPlayers(std::vector<PlayerBase *> &players) {
  DO_VALIDATION;
  players.push_back(referee);
  players.push_back(linesmen[0]);
  players.push_back(linesmen[1]);
}

void Officials::Process() {
  DO_VALIDATION;
  referee->Process();
  GetContext().tracker_disabled++;
  linesmen[0]->Process();
  linesmen[1]->Process();
  GetContext().tracker_disabled--;
}


void Officials::ProcessState(EnvState *state) {
  DO_VALIDATION;
  referee->ProcessStateBase(state);
  state->setValidate(false);
  linesmen[0]->ProcessStateBase(state);
  linesmen[1]->ProcessStateBase(state);
  state->setValidate(true);
}

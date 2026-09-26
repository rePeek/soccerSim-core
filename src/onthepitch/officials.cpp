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

#include "officials.hpp"

#include "../utils/objectloader.hpp"

#include "player/playerofficial.hpp"
#include "player/humanoid/humanoidbase.hpp"

#include "../data/playerdata.hpp"

#include "../main.hpp"

Officials::Officials(Match *match,
                     boost::intrusive_ptr<Node> fullbodySourceNode,
                     std::map<Vector3, Vector3> &colorCoords,
                     boost::shared_ptr<AnimCollection> animCollection)
    : match(match) {
  DO_VALIDATION;
  ObjectLoader loader;
  boost::intrusive_ptr<Node> playerNode = loader.LoadObject("media/objects/players/player.object");
  playerNode->SetName("player");
  playerNode->SetLocalMode(e_LocalMode_Absolute);

  playerData = new PlayerData();
  referee = new PlayerOfficial(e_OfficialType_Referee, match, playerData);
  linesmen[0] = new PlayerOfficial(e_OfficialType_Linesman, match, playerData);
  linesmen[1] = new PlayerOfficial(e_OfficialType_Linesman, match, playerData);

  referee->Activate(playerNode, fullbodySourceNode, colorCoords, match->GetAnimCollection(), false);
  linesmen[0]->Activate(playerNode, fullbodySourceNode, colorCoords, match->GetAnimCollection(), false);
  linesmen[1]->Activate(playerNode, fullbodySourceNode, colorCoords, match->GetAnimCollection(), false);
  playerNode->Exit();
  playerNode.reset();

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

void Officials::FetchPutBuffers() {
  DO_VALIDATION;
  referee->FetchPutBuffers();
  linesmen[0]->FetchPutBuffers();
  linesmen[1]->FetchPutBuffers();
}

void Officials::Put(bool mirror) {
  DO_VALIDATION;
  referee->Put(mirror);
  linesmen[0]->Put(mirror);
  linesmen[1]->Put(mirror);
}

void Officials::ProcessState(EnvState *state) {
  DO_VALIDATION;
  referee->ProcessStateBase(state);
  state->setValidate(false);
  linesmen[0]->ProcessStateBase(state);
  linesmen[1]->ProcessStateBase(state);
  state->setValidate(true);
}

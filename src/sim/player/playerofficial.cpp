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

#include "sim/player/playerofficial.hpp"

#include "sim/match.hpp"

#include "sim/player/controller/refereecontroller.hpp"

#include "env/main.hpp"

PlayerOfficial::PlayerOfficial(e_OfficialType officialType, Match *match,
                               PlayerData *playerData)
    : PlayerBase(match, playerData), officialType(officialType) {
}

PlayerOfficial::~PlayerOfficial() {}

HumanoidBase *PlayerOfficial::CastHumanoid() {
  return static_cast<HumanoidBase *>(humanoid.get());
}

RefereeController *PlayerOfficial::CastController() {
  return static_cast<RefereeController *>(controller.get());
}

void PlayerOfficial::Activate(bool lazyPlayer) {
  isActive = true;
  humanoid.reset(new HumanoidBase(this, match));

  CastHumanoid()->ResetPosition(Vector3(0), Vector3(0));
  SynchronizeKinematicState();
  BeginSimulationAction();

  controller.reset(new RefereeController(match));
  controller->SetPlayer(this);
}

void PlayerOfficial::Deactivate() {
  PlayerBase::Deactivate();
}

void PlayerOfficial::Process() {
  CastController()->Process();
  CastHumanoid()->Process();
  SynchronizeKinematicState();
  CheckSimulationActionOracle();
}

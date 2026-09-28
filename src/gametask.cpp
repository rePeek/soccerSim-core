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

#include "gametask.hpp"

#include "main.hpp"


GameTask::GameTask() { DO_VALIDATION; }

GameTask::~GameTask() {
  DO_VALIDATION;
  StopMatch();
}

void GameTask::StartMatch(std::unique_ptr<MatchSetup> setup, bool animations,
                          WorldState& world_state,
                          WorldProfiles& profiles) {
  DO_VALIDATION;
  randomize(GetScenarioConfig().game_engine_random_seed);
  assert(setup);
  assert(setup->match_data);
  assert(!match);
  match.reset(new Match(std::move(setup->match_data), GetControllers(), *setup,
                        animations, world_state, profiles));
}

bool GameTask::StopMatch() {
  DO_VALIDATION;
  if (match) {
    DO_VALIDATION;
    match->Exit();
    match.reset();
    return true;
  }
  return false;
}

void GameTask::ProcessPhase() {
  DO_VALIDATION;

  // The simulation tick. When this returns, every piece of
  // simulation-authoritative state for this tick is complete.
  match->Process();
}

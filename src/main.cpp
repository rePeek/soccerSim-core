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


#include <string>

#include "ai/ai_keyboard.hpp"
#include "base/log.hpp"
#include "base/math/math.hpp"
#include "base/utils.hpp"
#include "file.h"
#include "main.hpp"
#include "game_env.hpp"

using std::string;


using namespace blunted;

thread_local GameEnv* game;
Tracker tracker;

void DoValidation(int line, const char* file) {
  auto game = GetGame();
  if (game) {
    tracker.verify(line, file);
  }
}

GameEnv* GetGame() { return game; }
Tracker* GetTracker() { return &tracker; }

GameContext& GetContext() {
  return *game->context;
}

void SetGame(GameEnv* c) { game = c; }

std::shared_ptr<GameTask> GetGameTask() {
  return game->context->gameTask;
}


Properties* GetConfiguration() {
  return game->context->config;
}

ScenarioConfig& GetScenarioConfig() {
  return game->scenario_config;
}

GameConfig& GetGameConfig() {
  return game->game_config;
}

const std::vector<AIControlledKeyboard*>& GetControllers() {
  return game->context->controllers;
}

void randomize(unsigned int seed) {
  DO_VALIDATION;
  srand(seed);
  rand();  // Discard the first value before using the C RNG.
  randomseed(seed); // for the boost random
}

void run_game(Properties* input_config) {
  DO_VALIDATION;
  game->context->config = input_config;
  randomize(0);
  for (int x = 0; x < 2 * MAX_PLAYERS; x++) {
    DO_VALIDATION;
    e_PlayerColor color = e_PlayerColor(x % (e_PlayerColor_Default + 1));
    game->context->controllers.push_back(new AIControlledKeyboard(color));
  }
  // sequences

  game->context->gameTask = std::shared_ptr<GameTask>(new GameTask());
}
  // fire!

void quit_game() {
  DO_VALIDATION;
  game->context->gameTask.reset();

  for (unsigned int i = 0; i < game->context->controllers.size(); i++) {
    DO_VALIDATION;
    delete game->context->controllers[i];
  }
  game->context->controllers.clear();


  delete game->context->config;
}

void Tracker::verify_snapshot(long pos, int line, const char* file,
                              const std::string& trace) {
  bool failure = false;
  long waiting_pos = waiting_game->context->tracker_pos;
  if (pos != waiting_pos || line != waiting_line ||
      strcmp(file, waiting_file)) {
    failure = true;
  }
  if (!failure) {
    if (!game->context->gameTask->GetMatch()) return;
    EnvState reader1(game, "");
    game->ProcessState(&reader1);
    EnvState reader2(waiting_game, "", reader1.GetState());
    waiting_game->ProcessState(&reader2);
    failure = reader2.isFailure();
  }
  if (failure) {
    std::cout << "Validation range: " << start << " - " << end << std::endl;
    std::cout << "Position: " << pos << " vs " << waiting_pos << std::endl;
    std::cout << "Line: " << line << " vs " << waiting_line << std::endl;
    std::cout << "File: " << file << " vs " << waiting_file << std::endl;
    std::cout << "Stack: " << trace << " vs " << waiting_stack_trace
              << std::endl;
    std::cout << "Game ptr: " << game << " vs " << waiting_game << std::endl;
    Log(blunted::e_FatalError, "State comparison failure", "", "");
  }
}

void GameContext::ProcessState(EnvState* state) {
  for (int x = 0; x < sizeof(rng); x++) {
    state->process(((char*) &rng)[x]);
  }
  if (state->Load()) {
    EnvState reader(game, "");
    game->scenario_config.ProcessStateConstant(&reader);
    if (reader.GetState() != state->GetState().substr(state->getpos(),
        reader.GetState().length())) {
      Log(e_FatalError, "football", "set_state",
          "Current environment scenario != scenario in the state.");
    }
  }
  game->scenario_config.ProcessStateConstant(state);
  game->scenario_config.ProcessState(state);
#ifdef FULL_VALIDATION
  anims->ProcessState(state);
#endif
  state->process(step);
}

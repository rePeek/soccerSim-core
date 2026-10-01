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
#include "support/diagnostics/log.hpp"
#include "foundation/math/scalar.hpp"
#include "support/text/string_utils.hpp"
#include "support/io/file.hpp"
#include "env/main.hpp"
#include "env/game_env.hpp"
#include "env/rng.hpp"

using std::string;


using namespace blunted;

thread_local GameEnv* game;

GameEnv* GetGame() { return game; }

GameContext& GetContext() {
  return *game->context;
}

void SetGame(GameEnv* c) { game = c; }



Properties* GetConfiguration() {
  return game->context->config;
}

ScenarioConfig& GetScenarioConfig() {
  return game->scenario_config;
}

GameConfig& GetGameConfig() {
  return game->game_config;
}


void randomize(unsigned int seed) {
  srand(seed);
  rand();  // Discard the first value before using the C RNG.
  randomseed(seed); // for the boost random
}

void run_game(Properties* input_config) {
  game->context->config = input_config;
  randomize(0);
  for (int x = 0; x < 2 * kPlayersPerTeam; ++x) {
    const e_PlayerColor color =
        e_PlayerColor(x % (e_PlayerColor_Default + 1));
    auto* controller = new AIControlledKeyboard(color);
    game->context->checkpointControllers.push_back(controller);
    game->context->controllerSet.Add(*controller);
  }
  // sequences

  game->context->simulation = std::make_unique<Simulation>();
}
  // fire!

void quit_game() {
  game->context->simulation.reset();

  for (AIControlledKeyboard* controller : game->context->checkpointControllers) {
    delete controller;
  }
  game->context->checkpointControllers.clear();



  delete game->context->config;
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
  state->process(step);
}

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

#ifndef _GAME_ENV
#define _GAME_ENV

#include "sim/match.hpp"
#include "sim/gamedefines.hpp"
#include "env/gfootball_actions.h"
#include "env/main.hpp"

class AIControlledKeyboard;
class GameTask;

typedef std::vector<std::string> StringVector;

class ContextHolder {
 public:
  ContextHolder(GameEnv* game) : game(game) { SetGame(game); }
  ~ContextHolder() {
    if (GetGame() != game) {
      Log(e_FatalError, "football", "main", "game state was corrupted");
    }
  }
 private:
  const GameEnv* game;
};

// Game environment. This is the class that can be used directly from Python.
struct GameEnv {
  GameEnv() { }
  // Start the game (in separate process).
  void start_game();

  // Get the current state of the game (observation).
  SharedInfo get_info();

  // Executes the action inside the game.
  bool sticky_action_state(int action, bool left_team, int player);
  void action(int action, bool left_team, int player);
  void reset(const ScenarioConfig& game_config, bool init_animation);
  std::string get_state(const std::string& pickle);
  std::string set_state(const std::string& state);
  void step();
  void ProcessState(EnvState* state);
  ScenarioConfig& config();

 private:
  void setConfig(const ScenarioConfig& scenario_config);
  void do_step(int count);
  void getObservations();
  AIControlledKeyboard* keyboard_ = nullptr;
 public:
  ScenarioConfig scenario_config;
  GameConfig game_config;
  GameContext* context = nullptr;
  GameState state = game_created;
  int waiting_for_game_count = 0;
};

#endif

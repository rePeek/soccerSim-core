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

#ifndef _HPP_MAIN
#define _HPP_MAIN

class GameEnv;
GameEnv* GetGame();


#include "sim/simulation.hpp"
#include "sim/gamedefines.hpp"
#include "support/config/properties.hpp"
#include <memory>


enum e_RenderingMode {
  e_Disabled,
  e_Onscreen,
  e_Offscreen
};


enum GameState {
  game_created,
  game_initiated,
  game_running,
  game_done
};

class GameContext {
 public:
  GameContext() { }
  std::unique_ptr<Simulation> simulation;
  int stablePlayerCount = 0;
};

void SetGame(GameEnv* c);
GameContext& GetContext();



void run_game();
void quit_game();
int main(int argc, char** argv);


#endif

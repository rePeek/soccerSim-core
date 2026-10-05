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

#include <memory>

#include "model/team.hpp"
#include "model/pitch.hpp"
#include "support/diagnostics/log.hpp"
#include "sim/match.hpp"
#include "sim/gamedefines.hpp"
#include "env/main.hpp"
#include "control/player_control_set.hpp"


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
  GameEnv() = default;
  GameEnv(football::model::Team home, football::model::Team away,
          football::model::Pitch pitch);
  ~GameEnv();
  GameEnv(const GameEnv&) = delete;
  GameEnv& operator=(const GameEnv&) = delete;
  // The thread-local binding requires a stable environment address.
  GameEnv(GameEnv&&) = delete;
  GameEnv& operator=(GameEnv&&) = delete;
  // Start the game (in separate process).
  void start_game();
  // Creates a match in an initialized environment that has no active match.
  void init(const ScenarioConfig& game_config, bool init_animation);

  // Get the current state of the game (observation).
  SharedInfo get_info();
  PlayerControlSet& controls() { return controls_; }
  WorldState Observe() const;

  void reset(const ScenarioConfig& game_config, bool init_animation);
  std::string get_state(const std::string& pickle);
  std::string set_state(const std::string& state);
  void step();
  void ProcessState(EnvState* state);
  ScenarioConfig& config();

 private:
  void do_step(int count);
  void getObservations();
  // Empty descriptions retain the legacy default roster/formation path.
  football::model::Team home_team_;
  football::model::Team away_team_;
  football::model::Pitch pitch_;

 public:
  ScenarioConfig scenario_config;
  // Simulation ticks advanced by one environment step (10 ms per tick).
  int physics_steps_per_frame = 10;
  std::unique_ptr<GameContext> context;
  PlayerControlSet controls_;
  GameState state = game_created;
  int waiting_for_game_count = 0;
};

#endif

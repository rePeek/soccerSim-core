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

#ifndef FOOTBALL_ENV_GAME_ENV_HPP
#define FOOTBALL_ENV_GAME_ENV_HPP

#include <memory>

#include "control/player_control_set.hpp"
#include "model/pitch.hpp"
#include "model/team.hpp"
#include "state/world_state.hpp"

class GameContext;
class EnvState;
struct ScenarioConfig;

// Owns an explicitly declared match. One step advances one simulation tick.
class GameEnv {
 public:
  GameEnv(football::model::Team home, football::model::Team away,
          football::model::Pitch pitch);
  ~GameEnv();
  GameEnv(const GameEnv&) = delete;
  GameEnv& operator=(const GameEnv&) = delete;
  // Legacy thread-local runtime bindings require a stable address.
  GameEnv(GameEnv&&) = delete;
  GameEnv& operator=(GameEnv&&) = delete;

  void start_game();
  void reset_game();
  void stop_game();
  // Exactly one Simulation::Step (10 ms), not a GRF observation frame.
  void step();
  WorldState observe() const;
  PlayerControlSet& controls() { return controls_; }

 private:
  football::model::Team home_team_;
  football::model::Team away_team_;
  football::model::Pitch pitch_;
  std::unique_ptr<GameContext> context_;
  PlayerControlSet controls_;

  // Temporary implementation-only legacy episode input. Opaque in this header.
  std::unique_ptr<ScenarioConfig> scenario_config_;
  void init_legacy();

  // Transitional bridges, not a public runtime access API.
  friend class EnvState;
  friend GameContext& GetContext();
  friend ScenarioConfig& GetScenarioConfig();
};

#endif  // FOOTBALL_ENV_GAME_ENV_HPP

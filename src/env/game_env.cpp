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

#undef NDEBUG

#include "env/game_env.hpp"

#include <utility>

#include "env/main.hpp"
#include "env/model_adapter.hpp"
#include "support/diagnostics/assert.hpp"
#include "support/diagnostics/log.hpp"

namespace {

class ContextHolder {
 public:
  explicit ContextHolder(GameEnv* game) : game_(game) { SetGame(game); }
  ~ContextHolder() {
    if (GetGame() != game_) {
      Log(blunted::e_FatalError, "football", "main", "game state was corrupted");
    }
  }
 private:
  const GameEnv* game_;
};

}  // namespace

GameEnv::GameEnv(football::model::Team home, football::model::Team away,
                 football::model::Pitch pitch)
    : home_team_(std::move(home)),
      away_team_(std::move(away)),
      pitch_(std::move(pitch)),
      scenario_config_(new ScenarioConfig()) {}

GameEnv::~GameEnv() {
  stop_game();
}

void GameEnv::stop_game() {
  GameEnv* previous = GetGame();
  if (context_) {
    // Legacy teardown still uses GetContext(). Preserve another active game.
    SetGame(this);
    quit_game();
    context_.reset();
  }
  controls_.Clear();
  SetGame(previous == this ? nullptr : previous);
}

void GameEnv::start_game() {
  CHECK(!context_);
  context_ = std::make_unique<GameContext>();
  ContextHolder c(this);
  run_game();
  init_legacy();
}

void GameEnv::reset_game() {
  CHECK(context_ && context_->simulation);
  ContextHolder c(this);
  context_->simulation->Stop();
  init_legacy();
}

void GameEnv::step() {
  CHECK(context_ && context_->simulation);
  ContextHolder c(this);
  context_->simulation->Step(controls_);
}

WorldState GameEnv::observe() const {
  CHECK(context_ && context_->simulation);
  ContextHolder c(const_cast<GameEnv*>(this));
  return context_->simulation->Observe();
}

void GameEnv::init_legacy() {
  ContextHolder c(this);
  controls_.Clear();
  context_->step = -1;
  *scenario_config_ = ToRuntimeScenario(*ScenarioConfig::make(), home_team_, away_team_);
  context_->simulation->Init(home_team_, away_team_, pitch_, *scenario_config_,
                            context_->controllerSet, false);
}

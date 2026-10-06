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

#include "sim/simulation.hpp"
#include "support/diagnostics/assert.hpp"

#include "env/default_ai_setup.hpp"

GameEnv::GameEnv(football::model::Team home, football::model::Team away,
                 football::model::Pitch pitch)
    : home_team_(std::move(home)),
      away_team_(std::move(away)),
      pitch_(std::move(pitch)),
      ai_(football::env::MakeDefaultAI(home_team_, away_team_, pitch_)) {}

GameEnv::~GameEnv() {
  stop_game();
}

void GameEnv::stop_game() {
  simulation_.reset();
  controls_.Clear();
}

void GameEnv::start_game() {
  CHECK(!simulation_);
  // Publish only an initialized runtime; a rejected description leaves us stopped.
  auto simulation = std::make_unique<Simulation>();
  init_match(*simulation);
  simulation_ = std::move(simulation);
}

void GameEnv::reset_game() {
  CHECK(simulation_);
  simulation_->Stop();
  init_match(*simulation_);
}

void GameEnv::step() {
  CHECK(simulation_);
  const WorldState world = simulation_->Observe();
  PlayerControlSet combined;
  ai_.Update(world, combined);
  // Explicit controls override default decisions; no defaults are stored in
  // controls(), and human-owned actors are omitted by the value policy.
  for (const auto &control : controls_.controls()) combined.Set(control.player, control);
  simulation_->Step(combined);
}

WorldState GameEnv::observe() const {
  CHECK(simulation_);
  return simulation_->Observe();
}

void GameEnv::init_match(Simulation& simulation) {
  controls_.Clear();
  // The legacy episode defaults are now the only possible match options.
  simulation.Init(home_team_, away_team_, pitch_, MatchOptions{}, false);
}

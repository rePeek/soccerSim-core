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

#include "support/diagnostics/log.hpp"
#include "env/game_env.hpp"
#include "env/model_adapter.hpp"

#include <string>
#include <utility>

#include "support/diagnostics/assert.hpp"

using std::string;

namespace {

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

}  // namespace

GameEnv::GameEnv(football::model::Team home, football::model::Team away,
                 football::model::Pitch pitch)
    : home_team_(std::move(home)),
      away_team_(std::move(away)),
      pitch_(std::move(pitch)) {}

GameEnv::~GameEnv() {
  GameEnv* previous = GetGame();
  if (context) {
    // Legacy teardown uses GetContext(), so bind this environment until all
    // context-owned resources have been destroyed.
    SetGame(this);
    quit_game();
    context.reset();
  }
  SetGame(previous == this ? nullptr : previous);
}

void GameEnv::do_step(int count) {
  while (count--) {
    context->simulation->Step(controls_);
  }
}

void GameEnv::start_game() {
  assert(context == nullptr);
  context = std::make_unique<GameContext>();
  ContextHolder c(this);

  run_game();
  auto scenario_config = ScenarioConfig::make();
  init(*scenario_config, false);
}

SharedInfo GameEnv::get_info() {
  CHECK(physics_steps_per_frame > 0);
  SharedInfo info;
  context->simulation->GetState(&info);
  // Preserve the legacy observation division, but keep environment cadence
  // out of the simulation's raw state. Do not round-trip via env coordinates.
  for (auto* team : {&info.left_team, &info.right_team}) {
    for (PlayerInfo& player : *team) {
      player.player_direction /= physics_steps_per_frame;
    }
  }
  info.ball_direction /= physics_steps_per_frame;
  info.ball_rotation /= physics_steps_per_frame;
  info.step = context->step;
  return info;
}

WorldState GameEnv::Observe() const {
  return context->simulation->Observe();
}



std::string GameEnv::get_state(const std::string& pickle) {
  ContextHolder c(this);
  EnvState reader(this, "");
  string mutable_picke = pickle;
  reader.process(mutable_picke);
  ProcessState(&reader);
  return reader.GetState();
}

std::string GameEnv::set_state(const std::string& state) {
  SetGame(this);
  EnvState writer(this, state);
  string pickle;
  writer.process(pickle);
  ProcessState(&writer);
  if (!writer.eos()) {
    Log(e_FatalError, "football", "main", "corrupted state");
  }
  return pickle;
}

void GameEnv::step() {
  CHECK(physics_steps_per_frame > 0);
  do_step(physics_steps_per_frame);
  if (context->simulation->IsInPlay()) {
    context->step++;
  }
}

void GameEnv::ProcessState(EnvState* state) {
  state->process(this->state);
  state->process(waiting_for_game_count);
  context->ProcessState(state);
  context->simulation->ProcessState(state);
}

void GameEnv::init(const ScenarioConfig& game_config, bool animations) {
  ContextHolder c(this);
  controls_.Clear();
  context->step = -1;
  waiting_for_game_count = 0;
  scenario_config = ToRuntimeScenario(game_config, home_team_, away_team_);
  context->simulation->Init(home_team_, away_team_, pitch_, scenario_config,
                            context->controllerSet, animations);
}

void GameEnv::reset(const ScenarioConfig& game_config, bool animations) {
  context->simulation->Stop();
  init(game_config, animations);
}

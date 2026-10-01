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

#include "support/diagnostics/backtrace.hpp"
#include "support/diagnostics/log.hpp"
#include "env/game_env.hpp"

#include <cerrno>
#include <ctime>
#include <iostream>
#include <ratio>

#include "support/diagnostics/assert.hpp"
#include "support/io/file.hpp"

using std::string;

void GameEnv::do_step(int count) {
  while (count--) {
    context->simulation->Step(controls_);
  }
}

float Position::env_coord(int index) const {
  switch (index) {
    case 0:
      return value[0] / X_FIELD_SCALE;
    case 1:
      return value[1] / Y_FIELD_SCALE;
    case 2:
      return value[2] / Z_FIELD_SCALE;
    default:
      Log(e_FatalError, "football", "main", "index out of range");
      return 0;
  }
}

std::string Position::debug() {
  return std::to_string(value[0]) + "," + std::to_string(value[1]) + "," +
         std::to_string(value[2]);
}

std::unique_ptr<MatchConfig> GameEnv::BuildMatchConfig(
    const ScenarioConfig& scenario_config) {

  // ScenarioConfig belongs to the caller and stays in public pitch units. The
  // match owns this scaled copy; mutating the caller here used to flip the sign
  // bit of a zero y coordinate on every reset (+0 * negative scale -> -0).
  ScenarioConfig scaled_config = scenario_config;
  scaled_config.ball_position.coords[0] =
      scaled_config.ball_position.coords[0] * X_FIELD_SCALE;
  scaled_config.ball_position.coords[1] =
      scaled_config.ball_position.coords[1] * Y_FIELD_SCALE;

  CHECK(scaled_config.left_agents >= 0);
  CHECK(scaled_config.left_agents <= kPlayersPerTeam);
  CHECK(scaled_config.right_agents >= 0);
  CHECK(scaled_config.right_agents <= kPlayersPerTeam);

  // MatchData reads GetScenarioConfig() in its constructor, so publish the
  // scaled snapshot before constructing it rather than accidentally using the
  // previous reset's configuration.
  this->scenario_config = scaled_config;

  std::unique_ptr<MatchConfig> config(new MatchConfig());
  config->match_data.reset(new MatchData());
  return config;
}

void GameEnv::start_game() {
  assert(context == nullptr);
  install_stacktrace();
  std::cout.precision(17);
  context = new GameContext();
  ContextHolder c(this);
  // feenableexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
  std::cout << std::unitbuf;

  char* data_dir = getenv("GFOOTBALL_DATA_DIR");
  if (data_dir) {
    GetGameConfig().data_dir = data_dir;
  }
  Properties* config = new Properties();
  config->Set("match_duration", 0.027);
  config->Set("game", 0);
  run_game(config);
  auto scenario_config = ScenarioConfig::make();
  reset(*scenario_config, false);
}

SharedInfo GameEnv::get_info() {
  SharedInfo info;
  context->simulation->GetState(&info);
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
  // We do 10 environment steps per second, while game does 100 frames of
  // physics animation.
  do_step(GetGameConfig().physics_steps_per_frame);
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

void GameEnv::reset(const ScenarioConfig& game_config, bool animations) {
  ContextHolder c(this);
  controls_.Clear();
  context->step = -1;
  waiting_for_game_count = 0;
  auto match_config = BuildMatchConfig(game_config);
  randomize(game_config.game_engine_random_seed);
  context->simulation->Stop();
  context->simulation->Reset(std::move(match_config), context->controllerSet,
                             animations);
}

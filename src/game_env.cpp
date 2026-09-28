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

#include "game_env.hpp"

#include <cerrno>
#include <ctime>
#include <iostream>
#include <ratio>

#include "ai/ai_keyboard.hpp"
#include "file.h"
#include "gametask.hpp"

using std::string;

void GameEnv::do_step(int count) {
  DO_VALIDATION;
  while (count--) {
    DO_VALIDATION;
    world->Step();
  }
  if (context->gameTask->GetMatch()->IsInPlay()) {
    DoValidation(__LINE__, __FILE__);
  }
}

float Position::env_coord(int index) const {
  switch (index) {
    DO_VALIDATION;
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
  DO_VALIDATION;
  return std::to_string(value[0]) + "," + std::to_string(value[1]) + "," +
         std::to_string(value[2]);
}

void GameEnv::setConfig(const ScenarioConfig& scenario_config) {
  DO_VALIDATION;

  // ScenarioConfig belongs to the caller and stays in public pitch units. The
  // match owns this scaled copy; mutating the caller here used to flip the sign
  // bit of a zero y coordinate on every reset (+0 * negative scale -> -0).
  ScenarioConfig scaled_config = scenario_config;
  scaled_config.ball_position.coords[0] =
      scaled_config.ball_position.coords[0] * X_FIELD_SCALE;
  scaled_config.ball_position.coords[1] =
      scaled_config.ball_position.coords[1] * Y_FIELD_SCALE;

  CHECK(scaled_config.left_agents >= 0);
  CHECK(scaled_config.left_agents <= MAX_PLAYERS);
  CHECK(scaled_config.right_agents >= 0);
  CHECK(scaled_config.right_agents <= MAX_PLAYERS);

  // MatchData reads GetScenarioConfig() in its constructor, so publish the
  // scaled snapshot before constructing it rather than accidentally using the
  // previous reset's configuration.
  this->scenario_config = scaled_config;

  std::unique_ptr<MatchSetup> setup(new MatchSetup());
  setup->match_data.reset(new MatchData());
  setup->controllers.reserve(2 * MAX_PLAYERS);
  for (int controller = 0; controller < 2 * MAX_PLAYERS; ++controller) {
    ControllerSetup selection;
    selection.controller_id = controller;
    if (controller < scaled_config.left_agents) {
      selection.side = -1;
    } else if (controller >= MAX_PLAYERS &&
               controller < MAX_PLAYERS + scaled_config.right_agents) {
      selection.side = 1;
    }
    setup->controllers.push_back(selection);
  }
  context->matchSetup = std::move(setup);
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
    DO_VALIDATION;
    GetGameConfig().data_dir = data_dir;
  }
  Properties* config = new Properties();
  config->Set("match_duration", 0.027);
  config->Set("game", 0);
  run_game(config);
  world = std::make_unique<World>(*context->gameTask);
  auto scenario_config = ScenarioConfig::make();
  reset(*scenario_config, false);
  DO_VALIDATION;
}

SharedInfo GameEnv::get_info() {
  GetTracker()->setDisabled(true);
  SharedInfo info;
  GetGameTask()->GetMatch()->GetState(&info);
  info.step = context->step;
  GetTracker()->setDisabled(false);
  return info;
}

bool GameEnv::sticky_action_state(int action, bool left_team, int player) {
  SetGame(this);
  int controller_id = player + (left_team ? 0 : 11);
  auto controller =
      static_cast<AIControlledKeyboard*>(GetControllers()[controller_id]);
  switch (Action(action)) {
    case game_left:
      return controller->GetOriginalDirection() == Vector3(-1, 0, 0);
    case game_top_left:
      return controller->GetOriginalDirection() == Vector3(-1, 1, 0);
    case game_top:
      return controller->GetOriginalDirection() == Vector3(0, 1, 0);
    case game_top_right:
      return controller->GetOriginalDirection() == Vector3(1, 1, 0);
    case game_right:
      return controller->GetOriginalDirection() == Vector3(1, 0, 0);
    case game_bottom_right:
      return controller->GetOriginalDirection() == Vector3(1, -1, 0);
    case game_bottom:
      return controller->GetOriginalDirection() == Vector3(0, -1, 0);
    case game_bottom_left:
      return controller->GetOriginalDirection() == Vector3(-1, -1, 0);
    case game_keeper_rush:
      return controller->GetButton(e_ButtonFunction_KeeperRush);
    case game_pressure:
      return controller->GetButton(e_ButtonFunction_Pressure);
    case game_team_pressure:
      return controller->GetButton(e_ButtonFunction_TeamPressure);
    case game_sprint:
      return controller->GetButton(e_ButtonFunction_Sprint);
    case game_dribble:
      return controller->GetButton(e_ButtonFunction_Dribble);
    default:
      Log(e_FatalError, "football", "main", "invalid sticky action");
  }
  return false;
}

void GameEnv::action(int action, bool left_team, int player) {
  SetGame(this);
  GetTracker()->setDisabled(true);
  int controller_id = player + (left_team ? 0 : 11);
  auto controller = static_cast<AIControlledKeyboard*>(GetControllers()[controller_id]);
  controller->SetDisabled(false);
  switch (Action(action)) {
    case game_idle:
      break;
    case game_left:
      controller->SetDirection(Vector3(-1, 0, 0));
      break;
    case game_top_left:
      controller->SetDirection(Vector3(-1, 1, 0));
      break;
    case game_top:
      controller->SetDirection(Vector3(0, 1, 0));
      break;
    case game_top_right:
      controller->SetDirection(Vector3(1, 1, 0));
      break;
    case game_right:
      controller->SetDirection(Vector3(1, 0, 0));
      break;
    case game_bottom_right:
      controller->SetDirection(Vector3(1, -1, 0));
      break;
    case game_bottom:
      controller->SetDirection(Vector3(0, -1, 0));
      break;
    case game_bottom_left:
      controller->SetDirection(Vector3(-1, -1, 0));
      break;

    case game_long_pass:
      controller->SetButton(e_ButtonFunction_LongPass, true);
      break;
    case game_high_pass:
      controller->SetButton(e_ButtonFunction_HighPass, true);
      break;
    case game_short_pass:
      controller->SetButton(e_ButtonFunction_ShortPass, true);
      break;
    case game_shot:
      controller->SetButton(e_ButtonFunction_Shot, true);
      break;
    case game_keeper_rush:
      controller->SetButton(e_ButtonFunction_KeeperRush, true);
      break;
    case game_sliding:
      controller->SetButton(e_ButtonFunction_Sliding, true);
      break;
    case game_pressure:
      controller->SetButton(e_ButtonFunction_Pressure, true);
      break;
    case game_team_pressure:
      controller->SetButton(e_ButtonFunction_TeamPressure, true);
      break;
    case game_switch:
      controller->SetButton(e_ButtonFunction_Switch, true);
      break;
    case game_sprint:
      controller->SetButton(e_ButtonFunction_Sprint, true);
      break;
    case game_dribble:
      controller->SetButton(e_ButtonFunction_Dribble, true);
      break;
    case game_release_direction:
      controller->SetDirection(Vector3(0, 0, 0));
      break;
    case game_release_long_pass:
      controller->SetButton(e_ButtonFunction_LongPass, false);
      break;
    case game_release_high_pass:
      controller->SetButton(e_ButtonFunction_HighPass, false);
      break;
    case game_release_short_pass:
      controller->SetButton(e_ButtonFunction_ShortPass, false);
      break;
    case game_release_shot:
      controller->SetButton(e_ButtonFunction_Shot, false);
      break;
    case game_release_keeper_rush:
      controller->SetButton(e_ButtonFunction_KeeperRush, false);
      break;
    case game_release_sliding:
      controller->SetButton(e_ButtonFunction_Sliding, false);
      break;
    case game_release_pressure:
      controller->SetButton(e_ButtonFunction_Pressure, false);
      break;
    case game_release_team_pressure:
      controller->SetButton(e_ButtonFunction_TeamPressure, false);
      break;
    case game_release_switch:
      controller->SetButton(e_ButtonFunction_Switch, false);
      break;
    case game_release_sprint:
      controller->SetButton(e_ButtonFunction_Sprint, false);
      break;
    case game_release_dribble:
      controller->SetButton(e_ButtonFunction_Dribble, false);
      break;
    case game_builtin_ai:
      controller->SetDisabled(true);
      break;
  }
  GetTracker()->setDisabled(false);
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
  DO_VALIDATION;
  // We do 10 environment steps per second, while game does 100 frames of
  // physics animation.
  do_step(GetGameConfig().physics_steps_per_frame);
  if (context->gameTask->GetMatch()->IsInPlay()) {
    DO_VALIDATION;
    GetTracker()->setDisabled(true);
    context->step++;
    for (auto controller : GetControllers()) {
      DO_VALIDATION;
      controller->ResetNotSticky();
    }
    GetTracker()->setDisabled(false);
  }
}

void GameEnv::ProcessState(EnvState* state) {
  state->process(this->state);
  state->process(waiting_for_game_count);
  context->ProcessState(state);
  context->gameTask->GetMatch()->ProcessState(state);
}

void GameEnv::reset(const ScenarioConfig& game_config, bool animations) {
  DO_VALIDATION;
  ContextHolder c(this);
  // Reset call disables tracker.
  GetTracker()->setDisabled(true);
  context->step = -1;
  waiting_for_game_count = 0;
  setConfig(game_config);
  for (auto controller : GetControllers()) {
    DO_VALIDATION;
    controller->SetDisabled(true);
  }
  GetGameTask()->StopMatch();
  // Slots belong to World across matches. Clear unused slots as well.
  world->GetState().players = {};
  GetGameTask()->StartMatch(std::move(context->matchSetup), animations,
                            world->GetState(), world->GetProfiles());
}

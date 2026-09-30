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

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "env/defines.hpp"
#include "ai/ai_keyboard.hpp"
#include "controller/controller_set.hpp"

#include "env/game_env.hpp"
#include "env/main.hpp"

EnvState::EnvState(GameEnv* game, const std::string& state,
                   const std::string reference)
    : load(!state.empty()),
      state(state),
      reference(reference),
      scenario_config(&game->scenario_config),
      context(game->context) {
}

void EnvState::process(ControllerInput*& value) {
  void* raw = value;
  process(reinterpret_cast<void**>(&controllers[0]), controllers.size(), raw);
  value = static_cast<AIControlledKeyboard*>(raw);
}

void EnvState::ProcessControllerState(ControllerInput* controller) {
  auto* keyboard = dynamic_cast<AIControlledKeyboard*>(controller);
  if (!keyboard) {
    Log(blunted::e_FatalError, "EnvState", "controller",
        "controller does not support legacy checkpointing");
  }
  keyboard->ProcessState(this);
}

void EnvState::SetControllers(const ControllerSet& controller_set) {
  controllers.clear();
  for (auto* controller : controller_set.controllers()) {
    auto* keyboard = dynamic_cast<AIControlledKeyboard*>(controller);
    if (!keyboard) {
      Log(blunted::e_FatalError, "EnvState", "controller",
          "controller does not support legacy checkpointing");
    }
    controllers.push_back(keyboard);
  }
}
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

#ifndef _HPP_CORE_STATE_WORLD_STATE
#define _HPP_CORE_STATE_WORLD_STATE

#include <array>

#include "../../defines.hpp"
#include "ball_state.hpp"
#include "match_state.hpp"
#include "player_state.hpp"

// Schema and ownership slot for the football world state (Phase 6).
//
// Pure simulation data: no Physics*/Controller*/RulesEngine*/Match*/Humanoid*/
// Team* pointers. This is the container that later phases (Physics / Action /
// Rules / Replay) read from and write to.
//
// BallState is authoritative from Phase 7 onward (it lives here).
// PlayerState and MatchState are still migrating from the legacy model.
struct WorldState {
  unsigned long time_ms = 0;
  BallState ball;
  std::array<PlayerState, 2 * MAX_PLAYERS> players;
  MatchState match_state;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(time_ms);
    ball.ProcessState(state);
    for (PlayerState &player : players) {
      player.ProcessState(state);
    }
    match_state.ProcessState(state);
  }
};

#endif  // _HPP_CORE_STATE_WORLD_STATE
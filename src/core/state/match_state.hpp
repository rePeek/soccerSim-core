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

#ifndef _HPP_CORE_STATE_MATCH_STATE
#define _HPP_CORE_STATE_MATCH_STATE

#include "../../defines.hpp"

// Authoritative, behavior-free match-level state (Phase 6).
// Intentionally thin: goals and play state only. Richer match/rule state
// migrates here in later phases (rules/events).
struct MatchState {
  int left_goals = 0;
  int right_goals = 0;
  bool in_play = false;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(left_goals);
    state->process(right_goals);
    state->process(in_play);
  }
};

#endif  // _HPP_CORE_STATE_MATCH_STATE
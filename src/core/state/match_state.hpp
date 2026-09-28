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
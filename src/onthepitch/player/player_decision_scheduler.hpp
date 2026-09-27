// Copyright 2026
// Licensed under the Apache License, Version 2.0 (the "License");
#ifndef _HPP_PLAYER_DECISION_SCHEDULER
#define _HPP_PLAYER_DECISION_SCHEDULER

#include "../../defines.hpp"

// Simulation-owned clock for asking a player controller for its complete
// PlayerCommandQueue. Locomotion has a separate execution/publication clock.
struct PlayerDecisionScheduler {
  int nextRefreshTime_ms = 0;

  bool Due(int now_ms) const { return now_ms >= nextRefreshTime_ms; }

  void Schedule(int now_ms, int cadence_ms) {
    nextRefreshTime_ms = now_ms + cadence_ms;
  }

  // The deadline is gameplay state and must survive save/load.
  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(nextRefreshTime_ms);
  }
};

// Initial policy migrates the old world-context timing shape, but no longer
// depends on animation opportunities or animation/action lifecycle state.
inline int PlayerDecisionCadenceForContext_ms(bool designated_possession_player,
                                              bool designated_team_possession_player,
                                              float distance_to_ball) {
  if (designated_possession_player && distance_to_ball < 3.0f) return 20;
  if (designated_possession_player) return 30;
  if (designated_team_possession_player) return 40;
  if (distance_to_ball < 5.0f) return 50;
  if (distance_to_ball < 10.0f) return 80;
  return 240;
}

#endif

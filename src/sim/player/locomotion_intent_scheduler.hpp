// Copyright 2026
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef _HPP_LOCOMOTION_INTENT_SCHEDULER
#define _HPP_LOCOMOTION_INTENT_SCHEDULER

#include "env/defines.hpp"

// H3e4f-c2: the simulation decides when the controller is asked for a new
// locomotion intent, instead of inheriting the animation lifecycle's requeue
// opportunities (measured mean 236.7 ms, p50/p90 240 ms, max 760 ms).
//
// The initial cadence shape is deliberately inherited from the measured legacy
// behaviour rather than invented: the point of this step is to move ownership of
// the schedule, not to retune it. Distances are metres to the ball.
struct LocomotionIntentScheduler {
  int nextRefreshTime_ms = 0;
  int refreshes = 0;

  static int CadenceForDistance_ms(float distance_to_ball, bool has_possession) {
    if (has_possession) return 20;
    if (distance_to_ball < 5.0f) return 50;
    if (distance_to_ball < 10.0f) return 80;
    return 240;
  }

  bool Due(int now_ms) const { return now_ms >= nextRefreshTime_ms; }

  void Schedule(int now_ms, int cadence_ms) {
    nextRefreshTime_ms = now_ms + cadence_ms;
    ++refreshes;
  }

  // c2b: once this clock decides when the controller is queried it is gameplay
  // state, so it must survive save/load. refreshes stays telemetry and is
  // deliberately not serialized.
  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(nextRefreshTime_ms);
  }
};

#endif

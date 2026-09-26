//
//  player_locomotion.cpp
//  football
//
//  Copyright 2026
//

#include "player_locomotion.hpp"

// Single definitions for the reachability diagnostics. They live in the library
// so that the engine and the regression tool share one counter; see the comment
// in the header.
int &PlayerLocomotionInterceptSolverCalls() {
  static int calls = 0;
  return calls;
}

int &PlayerReachabilityRefreshes() {
  static int refreshes = 0;
  return refreshes;
}

int &PlayerReachabilityEligibleTicks() {
  static int ticks = 0;
  return ticks;
}

int &PlayerReachabilityReuses() {
  static int reuses = 0;
  return reuses;
}

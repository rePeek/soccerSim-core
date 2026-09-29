//
//  player_locomotion.cpp
//  football
//
//  Copyright 2026
//

#include "core/physics/player_movement.hpp"

namespace football_sim::physics {

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

}  // namespace football_sim::physics

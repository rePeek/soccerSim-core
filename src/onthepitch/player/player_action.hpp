//
//  player_action.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_ACTION
#define _HPP_PLAYER_ACTION

#include "../../defines.hpp"

// Explicit action timing consumed by match rules and collision logic. It is
// currently synchronized from Humanoid, but deliberately contains no
// animation, scene, or presentation objects.
struct PlayerActionState {
  e_FunctionType type = e_FunctionType_None;
  int frame = 0;
  int frameCount = 0;
  int elapsedTime_ms = 0;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(type);
    state->process(frame);
    state->process(frameCount);
    state->process(elapsedTime_ms);
  }
};

#endif

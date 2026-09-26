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
// synchronized from Humanoid after each actor tick, but deliberately contains
// no animation, scene, or presentation objects. Legacy controller queries keep
// their direct Humanoid reads until their intra-tick timing is migrated.
struct PlayerActionState {
  e_FunctionType type = e_FunctionType_None;
  int frame = 0;
  int frameCount = 0;
  int elapsedTime_ms = 0;
  int contactFrame = -1;
  Vector3 contactPosition = Vector3(0);

  bool HasScheduledContact() const { return contactFrame != -1; }
  bool IsContactPending() const {
    return HasScheduledContact() && frame < contactFrame;
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(type);
    state->process(frame);
    state->process(frameCount);
    state->process(elapsedTime_ms);
    state->process(contactFrame);
    state->process(contactPosition);
  }
};

#endif

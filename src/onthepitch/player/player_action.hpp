//
//  player_action.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_ACTION
#define _HPP_PLAYER_ACTION

#include "../../defines.hpp"

// Explicit action timing consumed by gameplay, match rules and collision logic.
// It is synchronized from Humanoid after each actor tick during H3d1, but
// contains no animation, scene or presentation objects. H3d2 will make its
// clock authoritative.
struct PlayerActionState {
  e_FunctionType type = e_FunctionType_None;
  int frame = 0;
  int frameCount = 0;
  int elapsedTime_ms = 0;
  int durationTime_ms = 0;
  int contactTime_ms = -1;
  int contactFrame = -1;
  Vector3 contactPosition = Vector3(0);

  bool HasScheduledContact() const { return contactTime_ms != -1; }
  bool IsContactPending() const {
    return HasScheduledContact() && elapsedTime_ms < contactTime_ms;
  }
  bool IsContactDue() const {
    return HasScheduledContact() && elapsedTime_ms >= contactTime_ms;
  }
  bool IsComplete() const {
    return durationTime_ms > 0 && elapsedTime_ms >= durationTime_ms;
  }

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    state->process(type);
    state->process(frame);
    state->process(frameCount);
    state->process(elapsedTime_ms);
    state->process(durationTime_ms);
    state->process(contactTime_ms);
    state->process(contactFrame);
    state->process(contactPosition);
  }
};

#endif

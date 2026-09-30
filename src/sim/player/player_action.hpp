//
//  player_action.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_ACTION
#define _HPP_PLAYER_ACTION

#include "env/defines.hpp"

// Explicit action timing consumed by gameplay, match rules and collision logic.
// PlayerActionExecutor is the authoritative producer: Humanoid only supplies
// the definition of a newly selected action. Gameplay, referee, collision,
// possession and the Humanoid lifecycle gates all read this state.
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
  bool IsAtLastFrame() const {
    return frameCount > 0 && frame >= frameCount - 1;
  }

  // The H3e locomotion authority boundary. Procedural locomotion may only
  // replace animation root motion for ordinary running with no ball
  // interaction. Every other case (contact scheduled on this action, the
  // actor retaining the ball, or any non-Movement action such as Shot, Pass,
  // Trap, BallControl, Sliding, Deflect, Trip and Special) must keep the
  // legacy animation motion, because the animation root path also drives the
  // touch vector and impulse algorithms.
  bool IsPureLocomotion(bool retains_ball) const {
    return type == e_FunctionType_Movement && !HasScheduledContact() &&
           !retains_ball;
  }

  void ProcessState(EnvState *state) {
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

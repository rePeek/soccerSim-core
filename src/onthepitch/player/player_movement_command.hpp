// Copyright 2026
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef _HPP_PLAYER_MOVEMENT_COMMAND
#define _HPP_PLAYER_MOVEMENT_COMMAND

#include "../../defines.hpp"

// H3e4f-a: simulation-owned storage for the Movement command that currently
// drives procedural locomotion.
//
// At this stage the legacy animation scheduler is still the producer: the value
// is written exactly when a selection accepts a Movement command, next to
// currentAnim.originatingCommand, and an oracle requires the two to agree on
// every field locomotion reads. That makes storage ownership a bit-exact
// refactor, so the producer (H3e4f-c) can be replaced later without also
// changing who owns the data.
struct PlayerMovementCommandState {
  PlayerCommand command;
  // False until the first Movement command is accepted. The kickoff animation
  // is selected without going through the two acceptance points, so the shadow
  // has no counterpart before that; the oracle is skipped there rather than
  // weakened. Instrumenting that initial selection is a follow-up.
  bool initialized = false;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    command.ProcessState(state);
    state->process(initialized);
  }
};

#endif

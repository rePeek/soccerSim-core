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
// Where the locomotion command came from. This exists so the producer cut can be
// measured rather than guessed: a direct Movement intent is not the same evidence
// as an accepted legacy action, and the BallControl/Trap coupling stays visible.
enum class LocomotionCommandSource {
  ActionCoupledIntent,
  DirectMovementIntent,
  // TEMPORARY (H3e4f-g0b-3b): a restart boundary republishes a command carried
  // forward from the animation state. Kept distinct from the target
  // SimulationSeed, which must be derived from simulation semantics instead.
  LegacyCarriedForwardIntent,
  // Target ownership. Defined now, not produced yet.
  SimulationSeed,
};

struct PlayerMovementCommandState {
  PlayerCommand command;
  LocomotionCommandSource source = LocomotionCommandSource::ActionCoupledIntent;
  // False until the first locomotion command is stored. The kickoff animation
  // is selected without going through an acceptance point, so the shadow has no
  // counterpart before that; the oracle is skipped there rather than weakened.
  bool initialized = false;

  void ProcessState(EnvState *state) {
    DO_VALIDATION;
    command.ProcessState(state);
    int source_value = static_cast<int>(source);
    state->process(source_value);
    source = static_cast<LocomotionCommandSource>(source_value);
    state->process(initialized);
  }
};

#endif

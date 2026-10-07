#ifndef FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP
#define FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP

#include "sim/observation/world_state.hpp"

class Match;

// Copies the simulation's current authoritative state into a passive value
// snapshot for decision systems, replay, training, and diagnostics.
WorldState BuildWorldState(const Match& match);

#endif  // FOOTBALL_SIM_OBSERVATION_WORLD_STATE_BUILDER_HPP

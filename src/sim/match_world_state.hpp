#ifndef FOOTBALL_SIM_MATCH_WORLD_STATE_HPP
#define FOOTBALL_SIM_MATCH_WORLD_STATE_HPP

#include "observation/world_state.hpp"

class Match;

// Copies the simulation's current authoritative state into a passive value
// snapshot for decision systems, replay, training, and diagnostics.
WorldState BuildWorldState(const Match& match);

#endif  // FOOTBALL_SIM_MATCH_WORLD_STATE_HPP

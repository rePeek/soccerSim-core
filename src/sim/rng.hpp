#ifndef FOOTBALL_SIM_RNG_HPP
#define FOOTBALL_SIM_RNG_HPP

#include "foundation/math/rng.hpp"

// This alias gives the generic algorithm its simulation-state authority only
// at the simulation boundary.
namespace blunted {
using SimulationRng = Rng;
}

#endif  // FOOTBALL_SIM_RNG_HPP

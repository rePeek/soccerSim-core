// Global random number entry points.
//
// The algorithms (engine, distribution, Uniform) live in
// foundation/math/rng.hpp. The deterministic simulation RNG is owned by
// Simulation and reached through Match; what is left here is only the
// presentation channel, which currently has no callers.
//
// Code that already holds a generator should call Rng::Uniform() directly
// instead of routing through a global.

#ifndef _HPP_ENV_RNG
#define _HPP_ENV_RNG

#include "foundation/math/scalar.hpp"
#include "foundation/math/rng.hpp"

namespace blunted {

using PresentationRng = Rng;

  void randomseed(unsigned int seed);
  // Presentation-only RNG. Must not affect simulation state.
  real random_non_determ(real min, real max);

}

#endif

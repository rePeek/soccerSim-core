// Global random number entry points.
//
// The algorithms (engine, distribution, Uniform) live in
// foundation/math/rng.hpp. What lives here is *authority*: these functions
// pick which context-owned generator to draw from, which is exactly why
// foundation must not declare or implement them.
//
// Code that already holds a generator should call Rng::Uniform() directly
// instead of routing through a global. Long term the deterministic generator
// should be reached through an explicit simulation owner.
#ifndef _HPP_ENV_RNG
#define _HPP_ENV_RNG

#include "foundation/math/bluntmath.hpp"

namespace blunted {

  void randomseed(unsigned int seed);
  // Deterministic simulation RNG. Presentation code must not use this:
  // consuming it shifts every subsequent simulation draw.
  real boostrandom(real min, real max);
  // Presentation-only RNG. Must not affect simulation state.
  real random_non_determ(real min, real max);

}

#endif

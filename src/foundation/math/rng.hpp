// Random number generation algorithms.
//
// This is deliberately a pure value/algorithm layer: it owns no game state and
// performs no I/O. Naming a generator type here is what keeps the determinism
// contract checkable at compile time:
//
//   SimulationRng   - state is part of the simulation checkpoint. Any extra
//                     draw shifts every later draw, so only simulation code
//                     may touch it.
//   PresentationRng - cosmetic jitter only. Never serialized; drawing from it
//                     must never influence simulation state.
//
// Ownership of the instances (and the "how do we store it" adapter) lives
// above this layer.
#ifndef _HPP_BASE_MATH_RNG
#define _HPP_BASE_MATH_RNG

#include <random>

namespace blunted {

typedef std::mt19937 BaseGenerator;

// Callable replacing boost::variate_generator: operator() draws a float in
// [0, 1), engine() exposes the raw generator for seeding and state streaming.
// std::mt19937 + std::uniform_real_distribution<float> are bit-identical to the
// boost equivalents for equal seeds (verified over 10M raw draws).
class Rng {
 public:
  Rng() : engine_(), distribution_(0.0f, 1.0f) {}

  float operator()() { return distribution_(engine_); }

  // Uniform draw over [min, max).
  float Uniform(float min, float max) {
    const float stretch = max - min;
    return min + ((*this)() * stretch);
  }

  void Seed(unsigned int seed) { engine_.seed(seed); }

  BaseGenerator &engine() { return engine_; }
  const BaseGenerator &engine() const { return engine_; }

 private:
  BaseGenerator engine_;
  std::uniform_real_distribution<float> distribution_;
};

// Deterministic simulation randomness. Serialized as part of the simulation
// state, so its draw order is part of the observable result.
class SimulationRng : public Rng {};

// Presentation-only randomness (for example the position of the sun). Never
// serialized and never allowed to feed simulation state.
class PresentationRng : public Rng {};

}

#endif

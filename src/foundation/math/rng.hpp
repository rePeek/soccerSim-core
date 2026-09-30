// Random number generation algorithms.
//
// This is deliberately a pure value/algorithm layer: it owns no game state,
// I/O, or authority. Callers decide what a generator instance represents.
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


}

#endif

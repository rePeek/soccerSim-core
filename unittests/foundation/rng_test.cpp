// Rng: determinism and output range for one implementation, not a stored
// golden sequence (the distribution is std-library implementation defined).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "football/foundation/math/rng.hpp"

TEST_CASE("Rng repeats exactly for the same seed", "[unit][foundation][rng]") {
  football::math::Rng first;
  football::math::Rng second;
  first.Seed(42u);
  second.Seed(42u);

  for (int i = 0; i < 256; ++i) {
    REQUIRE(first() == second());
  }
}

TEST_CASE("Rng different seeds diverge", "[unit][foundation][rng]") {
  football::math::Rng first;
  football::math::Rng second;
  first.Seed(1u);
  second.Seed(2u);

  bool any_difference = false;
  for (int i = 0; i < 64; ++i) {
    any_difference = any_difference || (first() != second());
  }
  CHECK(any_difference);
}

TEST_CASE("Rng unit draw stays in [0, 1)", "[unit][foundation][rng]") {
  football::math::Rng rng;
  rng.Seed(7u);
  for (int i = 0; i < 1000; ++i) {
    const float value = rng();
    CHECK(value >= 0.0f);
    CHECK(value < 1.0f);
  }
}

TEST_CASE("Rng Uniform stays in [min, max)", "[unit][foundation][rng]") {
  football::math::Rng rng;
  rng.Seed(11u);
  const float min = -3.0f;
  const float max = 7.0f;
  for (int i = 0; i < 1000; ++i) {
    const float value = rng.Uniform(min, max);
    CHECK(value >= min);
    CHECK(value < max);
  }
}

TEST_CASE("Rng exposes the raw engine for state streaming",
          "[unit][foundation][rng]") {
  football::math::Rng rng;
  rng.Seed(99u);
  std::vector<std::uint32_t> raw;
  for (int i = 0; i < 4; ++i) raw.push_back(rng.engine()());

  football::math::Rng replay;
  replay.Seed(99u);
  for (std::uint32_t expected : raw) {
    CHECK(replay.engine()() == expected);
  }
}

// ANIM-1 A6.3b.1 — final Movement pick rule tests.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "football/animation/select_movement.hpp"

TEST_CASE("a preferred clip that is a legal candidate is picked") {
  const std::vector<int> candidates{7, 3, 9};
  CHECK(ResolveMovementPick(candidates, 3) == 3);
  CHECK(ResolveMovementPick(candidates, 9) == 9);
}

TEST_CASE("a preferred clip outside the candidates falls back to the legacy first") {
  const std::vector<int> candidates{7, 3, 9};
  // 5 is not a legal candidate -> the stable-ordered first (7) plays.
  CHECK(ResolveMovementPick(candidates, 5) == 7);
}

TEST_CASE("an empty candidate set returns the preference unchanged") {
  const std::vector<int> empty;
  CHECK(ResolveMovementPick(empty, 42) == 42);
}

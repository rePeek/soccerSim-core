#include <catch2/catch_test_macros.hpp>

#include "sim/rules/goal.hpp"

namespace {
using blunted::Vector3;
using football::sim::CrossedGoalLine;
const auto pitch = football::model::Pitch{};

float GoalX(int side) {
  return (pitch.half_length() + pitch.line_half_width() + 0.11f) * side;
}

TEST_CASE("goal geometry tests the swept segment at either goal mouth", "[sim][goal]") {
  for (int side : {-1, 1}) {
    const float x = GoalX(side);
    for (float y : {-3.f, 0.f, 3.f}) {
      for (float z : {0.2f, 1.25f, 2.3f}) {
        const Vector3 previous(x - side, y, z);
        const Vector3 current(x + side, y, z);
        REQUIRE(CrossedGoalLine(pitch, side, previous, current));
        REQUIRE_FALSE(CrossedGoalLine(pitch, -side, previous, current));
        // Legacy geometry is bidirectional; do not introduce a direction filter.
        REQUIRE(CrossedGoalLine(pitch, side, current, previous));
      }
    }
    // Inside the goal already, or still on the pitch: no segment crossing.
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x + side, 0, 1), Vector3(x + side * 2, 0, 1)));
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x - side * 2, 0, 1), Vector3(x - side, 0, 1)));
  }
}

TEST_CASE("goal geometry retains strict segment endpoints and goal bounds", "[sim][goal]") {
  for (int side : {-1, 1}) {
    const float x = GoalX(side);
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x - side, 0, 1), Vector3(x, 0, 1)));
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x, 0, 1), Vector3(x + side, 0, 1)));
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x, -1, 1), Vector3(x, 1, 1))); // Coplanar, not a crossing.
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3(x - side, 0, 1), Vector3(x - side, 0, 1)));
    for (float y : {-4.f, 4.f}) {
      REQUIRE_FALSE(CrossedGoalLine(pitch, side,
          Vector3(x - side, y, 1), Vector3(x + side, y, 1)));
    }
    for (float z : {-0.1f, 2.6f}) {
      REQUIRE_FALSE(CrossedGoalLine(pitch, side,
          Vector3(x - side, 0, z), Vector3(x + side, 0, z)));
    }
    // Inclusive rectangle edges, as in the original triangles.
    REQUIRE(CrossedGoalLine(pitch, side,
        Vector3(x - side, 0, 0), Vector3(x + side, 0, 0)));
    REQUIRE(CrossedGoalLine(pitch, side,
        Vector3(x - side, 0, pitch.goal_height()),
        Vector3(x + side, 0, pitch.goal_height())));
  }
}

TEST_CASE("goal geometry rejects side-net entry using the legacy previous-position veto",
          "[sim][goal]") {
  for (int side : {-1, 1}) {
    const Vector3 current((pitch.half_length() + 0.3f) * side, 0, 1);
    // Both segments intersect the goal mouth; only the first starts outside the
    // post beyond the historical side-net threshold (including its double literals).
    REQUIRE_FALSE(CrossedGoalLine(pitch, side,
        Vector3((pitch.half_length() - 0.1f) * side, 4, 1), current));
    REQUIRE(CrossedGoalLine(pitch, side,
        Vector3((pitch.half_length() - 0.2f) * side, 4, 1), current));
  }
}

}  // namespace

// History contract: a passive recorder. It never reads the match, so these tests
// feed plain BallState values and link only football::history.
#include <catch2/catch_test_macros.hpp>

#include "football/history/history.hpp"

using football::ball::BallState;
using football::history::History;
using football::math::Vector3;
using football::time::Tick;

namespace {

BallState BallAt(float x) {
  BallState state;
  state.position = Vector3(x, 0.0f, 0.11f);
  return state;
}

}  // namespace

TEST_CASE("a recorder starts empty") {
  const History history;

  REQUIRE(history.enabled());
  REQUIRE(history.chunk().frames.empty());
}

TEST_CASE("a record keeps its tick and a copy of the state") {
  History history;
  BallState state = BallAt(1.0f);

  history.BuildSnapshot(Tick{7}, state);

  REQUIRE(history.chunk().frames.size() == 1);
  const auto& frame = history.chunk().frames.front();
  REQUIRE(frame.tick == Tick{7});
  REQUIRE(frame.ball == state);

  // A frame is a value: changing the source afterwards moves nothing.
  state.position = Vector3(2.0f, 0.0f, 0.11f);
  REQUIRE(frame.ball.position.coords[0] == 1.0f);
}

TEST_CASE("frames keep the tick they were handed, not their index") {
  History history;

  history.BuildSnapshot(Tick{100}, BallAt(0.0f));
  history.BuildSnapshot(Tick{250}, BallAt(1.0f));

  REQUIRE(history.chunk().frames[0].tick == Tick{100});
  REQUIRE(history.chunk().frames[1].tick == Tick{250});
}

TEST_CASE("the capacity hint sizes the chunk up front") {
  const History history(1000);

  REQUIRE(history.chunk().frames.capacity() >= 1000);
  REQUIRE(history.chunk().frames.empty());
}

TEST_CASE("a disabled recorder drops every frame and allocates nothing") {
  History history(1000, /*enabled=*/false);

  history.BuildSnapshot(Tick{1}, BallAt(0.0f));

  REQUIRE_FALSE(history.enabled());
  REQUIRE(history.chunk().frames.empty());
  REQUIRE(history.chunk().frames.capacity() == 0);
}

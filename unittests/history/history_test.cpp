#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

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

}

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
  History history(1000, false);

  history.BuildSnapshot(Tick{1}, BallAt(0.0f));

  REQUIRE_FALSE(history.enabled());
  REQUIRE(history.chunk().frames.empty());
  REQUIRE(history.chunk().frames.capacity() == 0);
}

TEST_CASE("snapshot reads copy consecutive ticks and return nullopt when unavailable") {
  History history;
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));

  auto state = BallAt(1.0f);
  state.orientation.Set(0.1f, 0.2f, 0.3f, 0.9f);
  history.BuildSnapshot(Tick{0}, state);
  history.BuildSnapshot(Tick{1}, BallAt(2.0f));
  const History& reader = history;
  auto snapshot = reader.ReadSnapshot(Tick{0});
  REQUIRE(snapshot);
  REQUIRE(snapshot->tick == Tick{0});
  REQUIRE(snapshot->ball == state);
  snapshot->ball.position = Vector3(42.0f, 0.0f, 0.0f);
  snapshot = reader.ReadSnapshot(Tick{0});
  REQUIRE(snapshot);
  REQUIRE(snapshot->ball == state);
  snapshot = reader.ReadSnapshot(Tick{1});
  REQUIRE(snapshot);
  REQUIRE(snapshot->tick == Tick{1});
  REQUIRE(snapshot->ball == BallAt(2.0f));
  REQUIRE_FALSE(reader.ReadSnapshot(Tick{2}));
  REQUIRE_FALSE(reader.ReadSnapshot(Tick{UINT64_MAX}));

  const History disabled(0, false);
  REQUIRE_FALSE(disabled.ReadSnapshot(Tick{0}));
}

TEST_CASE("snapshot reads are safe while recording reallocates the chunk") {
  History history;
  std::atomic<bool> done{false};
  std::jthread writer([&] {
    for (std::uint64_t i = 0; i < 2000; ++i) {
      history.BuildSnapshot(Tick{i}, BallAt(static_cast<float>(i)));
    }
    done.store(true);
  });

  while (!done.load()) {
    if (const auto snapshot = history.ReadSnapshot(Tick{1000})) {
      REQUIRE(snapshot->tick == Tick{1000});
      REQUIRE(snapshot->ball == BallAt(1000.0f));
    }
  }
  writer.join();
  const auto snapshot = history.ReadSnapshot(Tick{1999});
  REQUIRE(snapshot);
  REQUIRE(snapshot->tick == Tick{1999});
  REQUIRE(snapshot->ball == BallAt(1999.0f));
}

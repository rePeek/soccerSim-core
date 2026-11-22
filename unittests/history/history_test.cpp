#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <stdexcept>
#include <thread>

#include "football/history/history.hpp"

using football::ball::BallState;
using football::history::History;
using football::history::Recording;
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

TEST_CASE("recording stores a copy of the state") {
  History history;
  BallState state = BallAt(1.0f);
  history.RecordSnapshot(Tick{0}, state);

  REQUIRE(history.chunk().frames.size() == 1);
  const auto& frame = history.chunk().frames.front();
  REQUIRE(frame.tick == Tick{0});
  REQUIRE(frame.ball == state);

  state.position = Vector3(2.0f, 0.0f, 0.11f);
  REQUIRE(frame.ball.position.coords[0] == 1.0f);
}

TEST_CASE("recording requires consecutive ticks starting at zero") {
  History history;
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{100}, BallAt(0.0f)),
                    std::invalid_argument);
  REQUIRE(history.chunk().frames.empty());

  history.RecordSnapshot(Tick{0}, BallAt(0.0f));
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{0}, BallAt(1.0f)),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{2}, BallAt(2.0f)),
                    std::invalid_argument);
  REQUIRE(history.chunk().frames.size() == 1);

  history.RecordSnapshot(Tick{1}, BallAt(1.0f));
  for (std::uint64_t tick = 0; tick < 2; ++tick) {
    const auto snapshot = history.ReadSnapshot(Tick{tick});
    REQUIRE(snapshot);
    REQUIRE(snapshot->tick == Tick{tick});
    REQUIRE(snapshot->ball == BallAt(static_cast<float>(tick)));
  }
}

TEST_CASE("the capacity hint reserves the chunk up front") {
  const History history(1000);
  REQUIRE(history.chunk().frames.capacity() >= 1000);
  REQUIRE(history.chunk().frames.empty());
}

TEST_CASE("a disabled recorder drops every frame and allocates nothing") {
  History history(1000, Recording::Disabled);
  history.RecordSnapshot(Tick{1}, BallAt(0.0f));
  REQUIRE_FALSE(history.enabled());
  REQUIRE(history.chunk().frames.empty());
  REQUIRE(history.chunk().frames.capacity() == 0);
}

TEST_CASE("snapshot reads return independent copies or nullopt") {
  History history;
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));

  auto state = BallAt(1.0f);
  state.orientation.Set(0.1f, 0.2f, 0.3f, 0.9f);
  history.RecordSnapshot(Tick{0}, state);
  history.RecordSnapshot(Tick{1}, BallAt(2.0f));
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

  const History disabled(0, Recording::Disabled);
  REQUIRE_FALSE(disabled.ReadSnapshot(Tick{0}));
}

TEST_CASE("snapshot reads race with recording across chunk reallocations") {
  constexpr std::uint64_t frame_count = 2000;
  History history;
  std::barrier phase(2);
  std::jthread writer([&] {
    for (std::uint64_t tick = 0; tick < frame_count; ++tick) {
      phase.arrive_and_wait();
      history.RecordSnapshot(Tick{tick}, BallAt(static_cast<float>(tick)));
      phase.arrive_and_wait();
    }
  });

  bool snapshots_match_requested_ticks = true;
  for (std::uint64_t tick = 0; tick < frame_count; ++tick) {
    phase.arrive_and_wait();
    const auto concurrent_snapshot = history.ReadSnapshot(Tick{tick});
    phase.arrive_and_wait();
    const auto committed_snapshot = history.ReadSnapshot(Tick{tick});
    const auto matches = [tick](const auto& snapshot) {
      return snapshot && snapshot->tick == Tick{tick} &&
             snapshot->ball == BallAt(static_cast<float>(tick));
    };
    snapshots_match_requested_ticks &=
        (!concurrent_snapshot || matches(concurrent_snapshot)) &&
        matches(committed_snapshot);
  }
  writer.join();
  REQUIRE(snapshots_match_requested_ticks);
  REQUIRE(history.chunk().frames.size() == frame_count);
}

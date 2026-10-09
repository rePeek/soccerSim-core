#include <cstdlib>
#include <deque>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/observation/snapshot_history.hpp"

// This standalone executable observes allocations only inside the measured
// scope; Catch2 setup/assertions and owning query copies stay outside it.
namespace {
thread_local std::size_t* allocation_count = nullptr;
struct CountAllocations {
  std::size_t count = 0;
  CountAllocations() { allocation_count = &count; }
  ~CountAllocations() { allocation_count = nullptr; }
};
}

void* operator new(std::size_t size) {
  if (allocation_count) ++*allocation_count;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  if (allocation_count) ++*allocation_count;
  const auto align = static_cast<std::size_t>(alignment);
  if (size == 0) size = 1;
  if (size > std::numeric_limits<std::size_t>::max() - (align - 1)) throw std::bad_alloc();
  const auto rounded = ((size + align - 1) / align) * align;
  if (void* p = std::aligned_alloc(align, rounded)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

namespace {
using namespace football::sim::observation;
using football::sim::Tick;
using blunted::Vector3;

Snapshot MakeSnapshot(std::size_t players, float value = 1) {
  Snapshot snapshot;
  snapshot.ball = {Vector3(value, 2, 3), Vector3(4, value, 6), Vector3(7, 8, value)};
  snapshot.players.resize(players);
  for (std::size_t i = 0; i < players; ++i) {
    auto& player = snapshot.players[i];
    const auto v = value + static_cast<float>(i);
    player.position = Vector3(v, 10, 11);
    player.velocity = Vector3(12, v, 14);
    player.facing = Vector3(15, 16, v);
    player.body_facing = Vector3(v, 18, 19);
    player.animation_id = static_cast<AnimationId>(i + 20);
    player.frame = static_cast<std::uint32_t>(i + 30);
    player.active = i % 2 == 0;
    player.has_possession = i == 0;
  }
  return snapshot;
}

void RequireSnapshot(const Snapshot& actual, const Snapshot& expected) {
  REQUIRE(actual.ball.position == expected.ball.position);
  REQUIRE(actual.ball.velocity == expected.ball.velocity);
  REQUIRE(actual.ball.angular_velocity == expected.ball.angular_velocity);
  REQUIRE(actual.players.size() == expected.players.size());
  for (std::size_t i = 0; i < expected.players.size(); ++i) {
    const auto& a = actual.players[i];
    const auto& e = expected.players[i];
    REQUIRE(a.position == e.position);
    REQUIRE(a.velocity == e.velocity);
    REQUIRE(a.facing == e.facing);
    REQUIRE(a.body_facing == e.body_facing);
    REQUIRE(a.animation_id == e.animation_id);
    REQUIRE(a.frame == e.frame);
    REQUIRE(a.active == e.active);
    REQUIRE(a.has_possession == e.has_possession);
  }
}

TEST_CASE("snapshot defaults and fixed identity slots are value-only", "[snapshot]") {
  Snapshot snapshot;
  REQUIRE(snapshot.players.empty());
  REQUIRE(snapshot.ball.position == Vector3(0));
  REQUIRE(snapshot.ball.velocity == Vector3(0));
  REQUIRE(snapshot.ball.angular_velocity == Vector3(0));
  const PlayerSnapshot player;
  REQUIRE(player.position == Vector3(0));
  REQUIRE(player.velocity == Vector3(0));
  REQUIRE(player.facing == Vector3(1, 0, 0));
  REQUIRE(player.body_facing == Vector3(1, 0, 0));
  REQUIRE(player.animation_id == -1);
  REQUIRE(player.frame == 0);
  REQUIRE_FALSE(player.active);
  REQUIRE_FALSE(player.has_possession);
  SnapshotMetadata metadata{{1001, 2001, 4000000000U}};
  const auto copy = metadata;
  metadata.player_ids[0] = 99;
  REQUIRE(copy.player_ids[0] == 1001);
  REQUIRE(copy.player_ids[2] == 4000000000U);
}

TEST_CASE("snapshot history validates capacity and initialization", "[snapshot]") {
  REQUIRE_THROWS_AS(SnapshotHistory(0), std::invalid_argument);
  SnapshotHistory history;
  REQUIRE(history.Capacity() == 60000);
  REQUIRE(kSnapshotCapacity / football::sim::kTicksPerSecond == 600);
  REQUIRE(history.Empty());
  REQUIRE(history.Size() == 0);
  REQUIRE(history.Latest() == nullptr);
  REQUIRE(history.Oldest() == nullptr);
  REQUIRE(history.FindStep(0) == nullptr);
  REQUIRE(history.Find(Tick{}, 0) == nullptr);
  std::vector<SnapshotRecord> output(1);
  REQUIRE_FALSE(history.CopyLatest(1, output));
  REQUIRE(output.size() == 1);
  REQUIRE(history.CopyLatest(0, output));
  REQUIRE(output.empty());
  REQUIRE_THROWS_AS(history.Record({}, Snapshot{}), std::logic_error);
  history.Clear();
  REQUIRE_THROWS_AS(history.Record({}, Snapshot{}), std::logic_error);
  history.Reset(0);
  history.Record({}, Snapshot{});
  REQUIRE(history.Size() == 1);
  REQUIRE(history.Latest()->snapshot.players.empty());
}

TEST_CASE("snapshot history owns every field and preserves inactive slots", "[snapshot]") {
  SnapshotHistory history(3);
  history.Reset(5);
  auto source = MakeSnapshot(5);
  const auto expected = source;
  history.Record({0, Tick{12}, 4}, source);
  source = MakeSnapshot(5, 100);
  source.players[0].active = false;
  source.players[0].animation_id = -1;
  source.players[0].frame = 0;
  RequireSnapshot(history.Latest()->snapshot, expected);
  history.Record({1, Tick{13}, 4}, source);
  RequireSnapshot(history.Latest()->snapshot, source);
  RequireSnapshot(history.Oldest()->snapshot, expected);
  REQUIRE(history.Latest()->snapshot.players.size() == 5);
}

TEST_CASE("ring overwrite and windows match an independent chronological oracle", "[snapshot]") {
  for (std::size_t capacity : {1, 2, 3, 7, 32}) {
    SnapshotHistory history(capacity);
    history.Reset(2);
    std::deque<SnapshotRecord> oracle;
    for (std::uint64_t step = 0; step < 150; ++step) {
      const SnapshotStamp stamp{step * 3, Tick{step / 4}, step / 11};
      const auto snapshot = MakeSnapshot(2, static_cast<float>(step));
      history.Record(stamp, snapshot);
      oracle.push_back({stamp, snapshot});
      if (oracle.size() > capacity) oracle.pop_front();
      REQUIRE(history.Size() == oracle.size());
      REQUIRE(history.Capacity() == capacity);
      REQUIRE(history.Oldest()->stamp.step_index == oracle.front().stamp.step_index);
      REQUIRE(history.Latest()->stamp.step_index == oracle.back().stamp.step_index);
      for (const auto& record : oracle) {
        const auto* found = history.FindStep(record.stamp.step_index);
        REQUIRE(found != nullptr);
        RequireSnapshot(found->snapshot, record.snapshot);
        const SnapshotRecord* newest = nullptr;
        for (const auto& candidate : oracle) {
          if (candidate.stamp.timeline_tick == record.stamp.timeline_tick &&
              candidate.stamp.generation == record.stamp.generation) newest = &candidate;
        }
        const auto* by_tick = history.Find(record.stamp.timeline_tick, record.stamp.generation);
        REQUIRE(by_tick != nullptr);
        REQUIRE(by_tick->stamp.step_index == newest->stamp.step_index);
      }
      if (step >= capacity) REQUIRE(history.FindStep((step - capacity) * 3) == nullptr);
      REQUIRE(history.FindStep(stamp.step_index + 1) == nullptr);
      REQUIRE(history.Find(Tick{1000}, stamp.generation) == nullptr);
      REQUIRE(history.Find(stamp.timeline_tick, stamp.generation + 1) == nullptr);
      for (std::size_t count = 0; count <= oracle.size(); ++count) {
        std::vector<SnapshotRecord> output;
        REQUIRE(history.CopyLatest(count, output));
        REQUIRE(output.size() == count);
        for (std::size_t i = 0; i < count; ++i) {
          const auto& expected = oracle[oracle.size() - count + i];
          REQUIRE(output[i].stamp.step_index == expected.stamp.step_index);
          RequireSnapshot(output[i].snapshot, expected.snapshot);
        }
      }
    }
  }
}

TEST_CASE("exact tick lookup distinguishes resets duplicates and timeline jumps", "[snapshot]") {
  SnapshotHistory history(5);
  history.Reset(0);
  const Snapshot empty;
  history.Record({0, Tick{100}, 0}, empty);
  history.Record({1, Tick{100}, 0}, empty);
  history.Record({2, Tick{100}, 1}, empty);
  history.Record({4, Tick{100}, 1}, empty);
  history.Record({5, Tick{500}, 3}, empty);
  REQUIRE(history.Find(Tick{100}, 0)->stamp.step_index == 1);
  REQUIRE(history.Find(Tick{100}, 1)->stamp.step_index == 4);
  REQUIRE(history.Find(Tick{100}, 2) == nullptr);
  REQUIRE(history.Find(Tick{499}, 1) == nullptr);
  REQUIRE(history.Find(Tick{500}, 2) == nullptr);
  REQUIRE(history.FindStep(3) == nullptr);
  history.Record({6, Tick{500}, 3}, empty);
  history.Record({7, Tick{500}, 4}, empty);
  REQUIRE(history.Find(Tick{100}, 0) == nullptr);
  REQUIRE(history.Find(Tick{100}, 1)->stamp.step_index == 4);
  REQUIRE(history.Find(Tick{500}, 3)->stamp.step_index == 6);
  REQUIRE(history.Find(Tick{500}, 4)->stamp.step_index == 7);
  REQUIRE(history.Find(Tick{}, 0) == nullptr);
}

TEST_CASE("invalid records do not mutate a full ring", "[snapshot]") {
  SnapshotHistory history(1);
  history.Reset(2);
  const auto snapshot = MakeSnapshot(2);
  history.Record({7, Tick{10}, 3}, snapshot);
  REQUIRE_THROWS_AS(history.Record({8, Tick{11}, 3}, MakeSnapshot(1)), std::invalid_argument);
  REQUIRE_THROWS_AS(history.Record({7, Tick{10}, 3}, snapshot), std::logic_error);
  REQUIRE_THROWS_AS(history.Record({6, Tick{10}, 3}, snapshot), std::logic_error);
  REQUIRE_THROWS_AS(history.Record({8, Tick{9}, 3}, snapshot), std::logic_error);
  REQUIRE_THROWS_AS(history.Record({8, Tick{10}, 2}, snapshot), std::logic_error);
  REQUIRE(history.Size() == 1);
  REQUIRE(history.Latest()->stamp.step_index == 7);
  REQUIRE(history.Latest()->stamp.timeline_tick == Tick{10});
  REQUIRE(history.Latest()->stamp.generation == 3);
  RequireSnapshot(history.Latest()->snapshot, snapshot);
  history.Record({9, Tick{12}, 4}, MakeSnapshot(2, 9));
  REQUIRE(history.FindStep(7) == nullptr);
  REQUIRE(history.FindStep(9) == history.Latest());
}

TEST_CASE("clear and new-match reset invalidate queries and retain reusable storage", "[snapshot]") {
  SnapshotHistory history(3);
  history.Reset(2);
  const auto snapshot = MakeSnapshot(2);
  history.Record({5, Tick{10}, 4}, snapshot);
  std::vector<SnapshotRecord> copy;
  REQUIRE(history.CopyLatest(1, copy));
  history.Clear();
  REQUIRE(history.Empty());
  REQUIRE(history.Latest() == nullptr);
  REQUIRE(history.Oldest() == nullptr);
  REQUIRE(history.FindStep(5) == nullptr);
  REQUIRE(history.Find(Tick{10}, 4) == nullptr);
  history.Record({}, snapshot); // New sequence is permitted after Clear.
  RequireSnapshot(copy.front().snapshot, snapshot);
  REQUIRE(copy.front().stamp.step_index == 5);
  REQUIRE_FALSE(history.CopyLatest(2, copy));
  REQUIRE(copy.front().stamp.step_index == 5);
  history.Reset(2);
  REQUIRE(history.Empty());
  REQUIRE(history.FindStep(0) == nullptr);
  history.Reset(9);
  REQUIRE_THROWS_AS(history.Record({}, snapshot), std::invalid_argument);
  history.Record({}, MakeSnapshot(9));
  REQUIRE(history.Latest()->snapshot.players.size() == 9);
  history.Reset(0);
  history.Record({}, Snapshot{});
  REQUIRE(history.Latest()->snapshot.players.empty());
}

TEST_CASE("full-width stamps are searched without arithmetic narrowing or overflow", "[snapshot]") {
  const auto max = std::numeric_limits<std::uint64_t>::max();
  SnapshotHistory history(2);
  history.Reset(0);
  history.Record({max - 1, Tick{max - 1}, max - 1}, Snapshot{});
  history.Record({max, Tick{max}, max}, Snapshot{});
  REQUIRE(history.FindStep(max)->stamp.step_index == max);
  REQUIRE(history.Find(Tick{max}, max)->stamp.step_index == max);
  REQUIRE(history.FindStep(0) == nullptr);
  REQUIRE(history.Find(Tick{max}, max - 1) == nullptr);
  REQUIRE_THROWS_AS(history.Record({0, Tick{max}, max}, Snapshot{}), std::logic_error);
  REQUIRE(history.Size() == 2);
}

TEST_CASE("preallocated history records beyond the default capacity without allocations", "[snapshot]") {
  SnapshotHistory history;
  history.Reset(22);
  auto snapshot = MakeSnapshot(22);
  std::size_t allocations;
  bool queries_ok = true;
  {
    CountAllocations counter;
    for (std::uint64_t step = 0; step < 120123; ++step) {
      snapshot.ball.position.coords[0] = static_cast<float>(step);
      history.Record({step, Tick{step / 2}, step / 1000}, snapshot);
      queries_ok = queries_ok && history.FindStep(step) == history.Latest() &&
                   history.Find(Tick{step / 2}, step / 1000) == history.Latest();
    }
    history.Reset(22);
    history.Record({}, snapshot);
    history.Clear();
    history.Record({}, snapshot);
    allocations = counter.count;
  }
  REQUIRE(allocations == 0);
  REQUIRE(queries_ok);
  REQUIRE(history.Size() == 1);
  REQUIRE(history.Capacity() == kSnapshotCapacity);
}

}  // namespace

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/observation/snapshot.hpp"

namespace football::sim::observation {

// 100 Hz: 60,000 samples retain ten minutes of normally advancing steps.
inline constexpr std::size_t kSnapshotCapacity = 60000;

// Single simulation thread only; queries never perform disk I/O.
class SnapshotHistory {
 public:
  explicit SnapshotHistory(std::size_t capacity = kSnapshotCapacity);

  // Start a new match. Preallocate every player slot; recording before Reset
  // throws. Repeating Reset with the same player count reuses all allocations.
  void Reset(std::size_t player_count);

  // O(players), no allocations on successful writes. Step indices must strictly
  // increase (gaps are allowed); ticks and generations must not decrease.
  // Repeated ticks are valid. A rejected write leaves history unchanged.
  void Record(SnapshotStamp stamp, const Snapshot& snapshot);

  // Borrowed records are invalidated by overwriting their slot, Clear, Reset or
  // destruction. Do not retain these pointers in background consumers.
  const SnapshotRecord* Latest() const noexcept;
  const SnapshotRecord* Oldest() const noexcept;
  const SnapshotRecord* FindStep(std::uint64_t step_index) const noexcept;
  // O(log Size), exact tick/generation, latest matching executed step.
  const SnapshotRecord* Find(football::sim::Tick tick,
                             std::uint64_t generation) const noexcept;

  // Copy an owning window in oldest-to-newest order. Insufficient history returns
  // false without changing output; zero count succeeds and empties output.
  // Only this explicit copy API may allocate during a query.
  bool CopyLatest(std::size_t count, std::vector<SnapshotRecord>& output) const;

  // Invalidate all records, retaining preallocated slots and player topology.
  void Clear() noexcept;
  std::size_t Size() const noexcept { return size_; }
  std::size_t Capacity() const noexcept { return capacity_; }
  bool Empty() const noexcept { return size_ == 0; }

 private:
  const SnapshotRecord& At(std::size_t logical_index) const noexcept;

  std::vector<SnapshotRecord> frames_;
  std::size_t capacity_;
  std::size_t size_ = 0;
  std::size_t write_index_ = 0;
};

}  // namespace football::sim::observation

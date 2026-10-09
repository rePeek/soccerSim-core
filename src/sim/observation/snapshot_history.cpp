#include "sim/observation/snapshot_history.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace football::sim::observation {

SnapshotHistory::SnapshotHistory(std::size_t capacity) : capacity_(capacity) {
  if (capacity == 0) throw std::invalid_argument("snapshot capacity must be positive");
}

void SnapshotHistory::Reset(std::size_t player_count) {
  if (frames_.empty() || frames_.front().snapshot.players.size() != player_count) {
    // Build before publishing so allocation failure preserves the old history.
    std::vector<SnapshotRecord> frames(capacity_);
    for (auto& frame : frames) frame.snapshot.players.resize(player_count);
    frames_.swap(frames);
  }
  Clear();
}

void SnapshotHistory::Record(SnapshotStamp stamp, const Snapshot& snapshot) {
  if (frames_.empty()) throw std::logic_error("snapshot history is not initialized");
  if (snapshot.players.size() != frames_[write_index_].snapshot.players.size())
    throw std::invalid_argument("snapshot player count mismatch");
  if (const auto* latest = Latest()) {
    if (stamp.step_index <= latest->stamp.step_index)
      throw std::logic_error("snapshot step index must increase");
    if (stamp.timeline_tick < latest->stamp.timeline_tick)
      throw std::logic_error("snapshot timeline tick must not decrease");
    if (stamp.generation < latest->stamp.generation)
      throw std::logic_error("snapshot generation must not decrease");
  }

  auto& slot = frames_[write_index_];
  slot.snapshot.ball = snapshot.ball;
  std::copy(snapshot.players.begin(), snapshot.players.end(), slot.snapshot.players.begin());
  slot.stamp = stamp;
  // Publish only after all fields have been copied.
  write_index_ = (write_index_ + 1) % capacity_;
  if (size_ < capacity_) ++size_;
}

const SnapshotRecord& SnapshotHistory::At(std::size_t logical_index) const noexcept {
  const auto oldest = size_ == capacity_ ? write_index_ : 0;
  // Avoid overflowing an addition even for a large configured capacity.
  const auto remaining = capacity_ - oldest;
  const auto physical = logical_index < remaining ? oldest + logical_index
                                                  : logical_index - remaining;
  return frames_[physical];
}

const SnapshotRecord* SnapshotHistory::Latest() const noexcept {
  return Empty() ? nullptr : &At(size_ - 1);
}

const SnapshotRecord* SnapshotHistory::Oldest() const noexcept {
  return Empty() ? nullptr : &At(0);
}

const SnapshotRecord* SnapshotHistory::FindStep(std::uint64_t step_index) const noexcept {
  std::size_t first = 0, last = size_;
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    if (At(middle).stamp.step_index < step_index) first = middle + 1;
    else last = middle;
  }
  if (first == size_ || At(first).stamp.step_index != step_index) return nullptr;
  return &At(first);
}

const SnapshotRecord* SnapshotHistory::Find(football::sim::Tick tick,
                                           std::uint64_t generation) const noexcept {
  const auto key = std::pair(tick, generation);
  // Upper bound on the lexicographic (tick, generation) key selects the newest
  // duplicate, including when the matching range crosses the physical wrap.
  std::size_t first = 0, last = size_;
  while (first < last) {
    const auto middle = first + (last - first) / 2;
    const auto& stamp = At(middle).stamp;
    if (std::pair(stamp.timeline_tick, stamp.generation) <= key) first = middle + 1;
    else last = middle;
  }
  if (first == 0) return nullptr;
  const auto& record = At(first - 1);
  if (record.stamp.timeline_tick != tick || record.stamp.generation != generation)
    return nullptr;
  return &record;
}

bool SnapshotHistory::CopyLatest(std::size_t count,
                                 std::vector<SnapshotRecord>& output) const {
  if (count > size_) return false;
  output.resize(count);
  for (std::size_t i = 0; i < count; ++i) output[i] = At(size_ - count + i);
  return true;
}

void SnapshotHistory::Clear() noexcept {
  size_ = 0;
  write_index_ = 0;
}

}  // namespace football::sim::observation

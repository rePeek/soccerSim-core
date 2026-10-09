#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "sim/observation/snapshot.hpp"

namespace football::app::recording {

// No silent dropping. Block applies explicit producer backpressure; Fail aborts
// the archive session, reports overflow, and never writes a completion footer.
enum class QueueFullPolicy { Block, Fail };
struct SnapshotArchiveOptions {
  std::size_t chunk_frames = 100;
  std::size_t queue_capacity = 1000;
  QueueFullPolicy queue_full_policy = QueueFullPolicy::Block;
};

// One producer thread plus one private disk worker. Append copies values into a
// bounded, preallocated queue; no borrowed Simulation/history pointers escape.
class SnapshotArchive {
 public:
  SnapshotArchive();
  ~SnapshotArchive();
  SnapshotArchive(const SnapshotArchive&) = delete;
  SnapshotArchive& operator=(const SnapshotArchive&) = delete;

  void Open(const std::filesystem::path& path,
            const sim::observation::SnapshotMetadata& metadata,
            SnapshotArchiveOptions options = {});
  void Append(const sim::observation::SnapshotRecord& record);
  // Drain the queue, commit any partial chunk and flush the stream. This is an
  // explicit blocking API; OS power-loss durability (fsync) is not promised.
  void Flush();
  // Drain, write checksummed index/footer, join worker and report failures.
  // Call explicitly to observe errors; destructor is noexcept best-effort only.
  void Close();
  bool IsOpen() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Offline-only reader, independent of Simulation, Referee and AI. Strict mode
// requires a normal Close footer. Recovery exposes only the checksum-valid
// chronological prefix and marks it incomplete (never fabricates lost frames).
enum class ArchiveReadMode { CompleteOnly, RecoverPrefix };
class SnapshotArchiveReader {
 public:
  SnapshotArchiveReader();
  ~SnapshotArchiveReader();
  SnapshotArchiveReader(const SnapshotArchiveReader&) = delete;
  SnapshotArchiveReader& operator=(const SnapshotArchiveReader&) = delete;

  void Open(const std::filesystem::path& path,
            std::optional<std::uint64_t> expected_animation_hash = std::nullopt,
            ArchiveReadMode mode = ArchiveReadMode::CompleteOnly);
  const sim::observation::SnapshotMetadata& Metadata() const;
  bool Complete() const;
  std::uint64_t RecordCount() const;
  // Inclusive executed-step range; missing steps are not interpolated.
  std::vector<sim::observation::SnapshotRecord> ReadRange(
      std::uint64_t first_step, std::uint64_t last_step);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace football::app::recording

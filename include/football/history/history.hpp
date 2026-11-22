#pragma once

#include <cstddef>
#include <mutex>
#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

enum class Recording { Disabled, Enabled };

class History {
 public:
  explicit History(std::size_t expected_frames = 0,
                   Recording recording = Recording::Enabled);

  void RecordSnapshot(time::Tick tick, const ball::BallState& ball);

  std::optional<Snapshot> ReadSnapshot(time::Tick tick) const;

  const Chunk& chunk() const { return chunk_; }
  bool enabled() const { return enabled_; }

 private:
  Chunk chunk_;
  bool enabled_;
  mutable std::mutex mutex_;
};

}

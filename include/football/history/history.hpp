#pragma once

#include <cstddef>
#include <mutex>
#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

class History {
 public:
  explicit History(std::size_t expected_frames = 0, bool enabled = true);

  void BuildSnapshot(time::Tick tick, const ball::BallState& ball);

  std::optional<Snapshot> ReadSnapshot(time::Tick tick) const;

  const Chunk& chunk() const { return chunk_; }
  bool enabled() const { return enabled_; }

 private:
  Chunk chunk_;
  bool enabled_;
  mutable std::mutex mutex_;
};

}

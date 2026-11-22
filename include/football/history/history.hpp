#pragma once

#include <mutex>
#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

enum class Recording { Disabled, Enabled };

class History {
 public:
  explicit History(Recording recording = Recording::Enabled);

  void RecordSnapshot(time::Tick tick, const ball::BallState& ball);

  std::optional<Snapshot> ReadSnapshot(time::Tick tick) const;

  bool enabled() const { return enabled_; }

 private:
  Chunk chunk_;
  bool enabled_;
  mutable std::mutex mutex_;
};

}

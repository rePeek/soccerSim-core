#include "football/history/history.hpp"

namespace football::history {

History::History(std::size_t expected_frames, bool enabled) : enabled_(enabled) {
  if (enabled_) chunk_.frames.reserve(expected_frames);
}

void History::BuildSnapshot(time::Tick tick, const ball::BallState& ball) {
  if (!enabled_) return;
  std::lock_guard lock(mutex_);
  chunk_.frames.push_back(Snapshot{tick, ball});
}

std::optional<Snapshot> History::ReadSnapshot(time::Tick tick) const {
  std::lock_guard lock(mutex_);
  if (tick.value >= chunk_.frames.size()) return std::nullopt;
  return chunk_.frames[tick.value];
}

}

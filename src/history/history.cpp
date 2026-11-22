#include "football/history/history.hpp"

#include <stdexcept>

namespace football::history {

History::History(Recording recording) : enabled_(recording == Recording::Enabled) {}

void History::RecordSnapshot(time::Tick tick, const ball::BallState& ball) {
  if (!enabled_) return;
  std::lock_guard lock(mutex_);
  if (tick.value != chunk_.frames.size()) {
    throw std::invalid_argument("history ticks must start at zero and be consecutive");
  }
  chunk_.frames.push_back(Snapshot{tick, ball});
}

std::optional<Snapshot> History::ReadSnapshot(time::Tick tick) const {
  std::lock_guard lock(mutex_);
  if (tick.value >= chunk_.frames.size()) return std::nullopt;
  return chunk_.frames[tick.value];
}

}

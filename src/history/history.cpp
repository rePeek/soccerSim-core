#include "football/history/history.hpp"

#include <stdexcept>
#include <utility>

namespace football::history {

History::History() : mode_(HistoryMode::Recording) {}

History::History(Chunk chunk) : chunk_(std::move(chunk)), mode_(HistoryMode::Replay) {}

void History::RecordSnapshot(time::Tick tick, const ball::BallState& ball) {
  if (mode_ == HistoryMode::Replay) {
    throw std::logic_error("history cannot record in replay mode");
  }
  if (tick.value != chunk_.frames.size()) {
    throw std::invalid_argument("history ticks must start at zero and be consecutive");
  }
  chunk_.frames.push_back(Snapshot{tick, ball});
}

std::optional<Snapshot> History::ReadSnapshot(time::Tick tick) const {
  if (tick.value >= chunk_.frames.size()) return std::nullopt;
  return chunk_.frames[tick.value];
}

}

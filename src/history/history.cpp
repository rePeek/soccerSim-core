#include "football/history/history.hpp"

#include <stdexcept>
#include <utility>

namespace football::history {

History::History()
    : mode_(HistoryMode::Recording), state_(std::make_unique<State>()) {}

History::History(Chunk chunk)
    : mode_(HistoryMode::Replay), state_(std::make_unique<State>()) {
  state_->chunk = std::move(chunk);
}

void History::RecordSnapshot(time::Tick tick, const ball::BallState& ball) {
  if (mode_ == HistoryMode::Replay) {
    throw std::logic_error("history cannot record in replay mode");
  }
  std::lock_guard lock(state_->mutex);
  if (tick.value != state_->chunk.frames.size()) {
    throw std::invalid_argument("history ticks must start at zero and be consecutive");
  }
  state_->chunk.frames.push_back(Snapshot{tick, ball});
}

std::optional<Snapshot> History::ReadSnapshot(time::Tick tick) const {
  std::lock_guard lock(state_->mutex);
  if (tick.value >= state_->chunk.frames.size()) return std::nullopt;
  return state_->chunk.frames[tick.value];
}

}

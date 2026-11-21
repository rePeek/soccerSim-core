#include "football/history/history.hpp"

namespace football::history {

History::History(std::size_t expected_frames, bool enabled) : enabled_(enabled) {
  if (enabled_) chunk_.frames.reserve(expected_frames);
}

void History::BuildSnapshot(time::Tick tick, const ball::BallState& ball) {
  if (!enabled_) return;
  chunk_.frames.push_back(Snapshot{tick, ball});
}

}  // namespace football::history

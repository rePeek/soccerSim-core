#pragma once

#include <cstddef>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

// Records what a match looked like at a tick. It pulls nothing from the match:
// the caller hands it the authoritative state, so history depends on the state
// types only and never on Ball, Env or the runner.
//
// Recording is a construction-time choice: a disabled recorder drops every frame
// and allocates nothing. There is no way to enable it afterwards.
class History {
 public:
  // expected_frames sizes the chunk up front. A full match is kFullTime + 1
  // frames of 64 bytes (~33 MiB), faulted in only as frames are written.
  explicit History(std::size_t expected_frames = 0, bool enabled = true);

  // Appends the state at `tick`. Does nothing while disabled.
  void BuildSnapshot(time::Tick tick, const ball::BallState& ball);

  const Chunk& chunk() const { return chunk_; }
  bool enabled() const { return enabled_; }

 private:
  Chunk chunk_;
  bool enabled_;
};

}  // namespace football::history

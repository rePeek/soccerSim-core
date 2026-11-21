#pragma once

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::history {

// One recorded frame: the match state at a tick. Fixed-size and allocation-free
// on purpose: a Chunk is one contiguous vector of frames, so recording must never
// allocate per frame. Any variable-length or sparse data belongs in a separate
// structure, not here.
//
// Only the tick is stored: phase is a rule derived from it, and that rule lives
// with Env. History must never include env.hpp (Env owns a History), so a frame
// that needs a phase has to move PhaseOfTick out first.
struct Snapshot {
  time::Tick tick;
  ball::BallState ball;

  bool operator==(const Snapshot&) const = default;
};

static_assert(sizeof(Snapshot) == 64, "64 bytes per frame: ~33 MiB per match");

}  // namespace football::history

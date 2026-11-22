#pragma once

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"

namespace football::history {

struct Snapshot {
  time::Tick tick;
  ball::BallState ball;

  bool operator==(const Snapshot&) const = default;
};

static_assert(sizeof(Snapshot) == 64, "snapshot layout must remain 64 bytes");

}

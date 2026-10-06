#ifndef FOOTBALL_AI_TEAM_REQUESTS_HPP
#define FOOTBALL_AI_TEAM_REQUESTS_HPP

#include <cstdint>
#include <optional>

#include "model/player.hpp"

namespace football::ai {

// Short-lived decision intent, separate from the persistent TacticalBoard.
// Clocked by observed simulation ticks (10 ms), never wall time or actor pointers.
struct TimedPlayerIntent {
  std::optional<model::PlayerId> player;
  std::optional<model::PlayerId> marking_target;
  std::uint64_t issued_tick = 0;
  std::uint64_t duration_ticks = 0;
  std::uint64_t reset_sequence = 0;

  bool Active(std::uint64_t tick, std::uint64_t sequence) const {
    return player && sequence == reset_sequence && tick >= issued_tick &&
           tick - issued_tick < duration_ticks;
  }
};

struct TeamRequests {
  TimedPlayerIntent attacking_run;
  TimedPlayerIntent pressure;
  TimedPlayerIntent keeper_rush;
};

}  // namespace football::ai
#endif  // FOOTBALL_AI_TEAM_REQUESTS_HPP

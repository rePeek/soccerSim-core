#ifndef FOOTBALL_AI_TACTICAL_BOARD_HPP
#define FOOTBALL_AI_TACTICAL_BOARD_HPP

#include <cstdint>

#include "football/ai/tactical_plan.hpp"
#include "football/pitch/pitch.hpp"
#include "football/runtime/match_config.hpp"

namespace football::ai {

// Persistent AI-owned configuration: one team's tactical plan plus a monotonic
// revision bumped on every accepted coach write. External coaches/UI may edit
// these values; actual restarts belong to sim/WorldState.
//
// The plan is the single authority for tactical intent. There is no second,
// legacy representation and no mode switch: every reader consumes the plan (or
// the TacticalInterpreter's projection of it). `revision` is the concurrency
// token for stale writes.
struct TacticalBoard {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  TacticalPlan plan;
  std::uint64_t revision = 0;
};

// Pure structural validation of the board's plan.
inline bool ValidateTacticalBoard(const TacticalBoard &board) {
  return ValidateTacticalPlan(board.plan).ok;
}

// Bootstrap the formal plan from the declared static team shape.
TacticalBoard MakeTacticalBoard(const football::sim::TeamSetup &team,
                                football::sim::TeamSide side,
                                const football::pitch::Pitch &pitch);

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_BOARD_HPP

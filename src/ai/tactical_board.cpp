#include "football/ai/tactical_board.hpp"

namespace football::ai {

TacticalBoard MakeTacticalBoard(const football::sim::TeamSetup &team,
                               football::sim::TeamSide side,
                               const football::pitch::Pitch &pitch) {
  TacticalBoard board;
  board.side = side;
  board.plan = MakeTacticalPlan(team, side, pitch);
  return board;
}

}  // namespace football::ai

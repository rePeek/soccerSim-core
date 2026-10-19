#ifndef FOOTBALL_AI_COACH_TOOL_EXECUTOR_HPP
#define FOOTBALL_AI_COACH_TOOL_EXECUTOR_HPP

#include <optional>
#include <string>

#include "coach_tool_decoder.hpp"
#include "football/ai/coach_command.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Everything one execution may read. The world is the observed, read-only state
// and the board is the coach's own persistent plan; the executor never reads the
// opponent's board and never mutates either. Null pointers mean "unavailable"
// and yield a rejected payload instead of a crash.
struct CoachToolView {
  football::sim::TeamSide self = football::sim::TeamSide::Home;
  const WorldState* world = nullptr;
  const TacticalBoard* board = nullptr;  // own side only
};

// The outcome of one tool call. A read resolves to a JSON payload. A write is
// decoded and *staged*, never applied: `command` carries it to the turn's single
// commit boundary, and the payload says "staged" so the model never believes a
// change took effect inside the same turn.
struct CoachToolExecution {
  bool ok = false;
  std::string content_json;
  std::optional<CoachCommand> command;
};

// Turns a decoded request into a read payload or a staged command. It owns no
// state and no board. World-relative write validation (marking ownership, board
// revision, staleness) belongs to the commit boundary, not here.
class CoachToolExecutor {
 public:
  static CoachToolExecution Execute(const CoachToolRequest& request,
                                    const CoachToolView& view);
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_EXECUTOR_HPP

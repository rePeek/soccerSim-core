#ifndef FOOTBALL_AI_COACH_COMMAND_EXECUTOR_HPP
#define FOOTBALL_AI_COACH_COMMAND_EXECUTOR_HPP

#include <cstdint>

#include "football/ai/coach_command.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// Applies one validated command to one side's board as an atomic transaction.
// The board is copied, the command is validated against the world, and only a
// fully valid command commits and bumps the revision. A failure leaves the
// board and its revision untouched.
//
// The executor owns no state and no board; the caller keeps the single
// authority. It never touches Simulation, Rules or the match result.
class CoachCommandExecutor {
 public:
  CoachCommandResult Apply(TacticalBoard& board,
                           const CoachCommandEnvelope& envelope,
                           const WorldState& world, std::uint64_t tick) const;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_COMMAND_EXECUTOR_HPP

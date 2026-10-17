#ifndef FOOTBALL_AI_COACH_POLICY_HPP
#define FOOTBALL_AI_COACH_POLICY_HPP

#include <vector>

#include "football/ai/coach_command.hpp"
#include "football/ai/coach_context.hpp"

namespace football::ai {

// The coach decision boundary. v1 ships deterministic heuristic
// implementations; a future local/online LLM provider implements the same
// interface behind an adapter that parses tool_calls into CoachCommand values.
//
// RunTurn is synchronous and offline. It must never block the simulation on
// network I/O; async providers observe and submit on their own schedule.
class ICoachPolicy {
 public:
  virtual ~ICoachPolicy() = default;
  virtual void Reset() {}
  // Produces zero or more typed commands for this turn. Emitting nothing is a
  // valid NoOp.
  virtual void RunTurn(const CoachContext& context,
                       std::vector<CoachCommand>& out) = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_POLICY_HPP

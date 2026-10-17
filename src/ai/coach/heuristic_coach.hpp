#ifndef FOOTBALL_AI_HEURISTIC_COACH_HPP
#define FOOTBALL_AI_HEURISTIC_COACH_HPP

#include "football/ai/coach_policy.hpp"

namespace football::ai {

// First deterministic coach policy. It is never installed by default: tests and
// explicit configuration opt in. It reads the score and phase from the context
// and emits bounded team-instruction edits, only when a field actually differs
// from the target (so repeated turns do not spam the board).
class HeuristicCoachPolicy final : public ICoachPolicy {
 public:
  void Reset() override {}
  void RunTurn(const CoachContext& context,
               std::vector<CoachCommand>& out) override;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_HEURISTIC_COACH_HPP

#ifndef FOOTBALL_AI_COACH_SESSION_HPP
#define FOOTBALL_AI_COACH_SESSION_HPP

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "agent_protocol.hpp"
#include "coach_tool_executor.hpp"
#include "football/ai/coach_context.hpp"

namespace football::ai {

// One turn's read-only inputs: the reasoning context plus the world and the
// coach's own board that tools may read. It owns nothing; the caller keeps the
// authoritative world and board.
struct CoachTurnSnapshot {
  CoachContext context;
  CoachToolView view;
};

// Owns one team's conversation with the model. History is kept per turn, so a
// trim always drops a complete user/tool exchange and never orphans a tool
// message from the assistant tool call it answers. The system prompt is fixed
// and always first. Context compaction is a later stage (AGENT-5).
class CoachSession {
 public:
  static constexpr std::size_t kDefaultMaxTurns = 8;

  explicit CoachSession(football::sim::TeamSide self,
                        std::size_t max_turns = kDefaultMaxTurns);

  // Starts a turn: appends the observed context as the user message.
  void BeginTurn(const CoachTurnSnapshot& snapshot);
  // Appends one assistant or tool message to the current turn.
  void Record(const AgentMessage& message);

  // The flattened conversation in provider order: system, then every turn.
  const std::vector<AgentMessage>& Messages() const;

  football::sim::TeamSide self() const { return self_; }
  std::size_t TurnCount() const { return turns_.size(); }
  void Reset();

 private:
  std::string SystemPrompt() const;

  football::sim::TeamSide self_;
  std::size_t max_turns_;
  AgentMessage system_;
  std::deque<std::vector<AgentMessage>> turns_;
  mutable std::vector<AgentMessage> flat_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_SESSION_HPP

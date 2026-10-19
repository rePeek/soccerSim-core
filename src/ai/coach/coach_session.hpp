#ifndef FOOTBALL_AI_COACH_SESSION_HPP
#define FOOTBALL_AI_COACH_SESSION_HPP

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "coach_tool_executor.hpp"
#include "football/ai/coach_context.hpp"
#include "football/model/model_protocol.hpp"

namespace football::ai {

// One turn's read-only inputs: the reasoning context plus the world and the
// coach's own board that tools may read. It owns nothing; the caller keeps the
// authoritative world and board.
//
// NOTE (AGENT-4): the pointers below are valid only while the turn runs on the
// simulation thread. A turn that reasons on a worker thread must be handed an
// owning, immutable snapshot instead, so tools never read a live board that may
// move or die mid-turn.
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
  void Record(const football::model::ModelMessage& message);

  // The flattened conversation in provider order: system, then every turn.
  const std::vector<football::model::ModelMessage>& Messages() const;

  football::sim::TeamSide self() const { return self_; }
  std::size_t TurnCount() const { return turns_.size(); }
  void Reset();

 private:
  std::string SystemPrompt() const;

  football::sim::TeamSide self_;
  std::size_t max_turns_;
  football::model::ModelMessage system_;
  std::deque<std::vector<football::model::ModelMessage>> turns_;
  mutable std::vector<football::model::ModelMessage> flat_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_SESSION_HPP

#include "coach_session.hpp"

#include <utility>

#include "coach_tool_json.hpp"

namespace football::ai {
namespace {
using football::sim::TeamSide;
}  // namespace

CoachSession::CoachSession(TeamSide self, std::size_t max_turns)
    : self_(self), max_turns_(max_turns == 0 ? 1 : max_turns) {
  system_.role = AgentRole::System;
  system_.content = SystemPrompt();
}

std::string CoachSession::SystemPrompt() const {
  const char* side = self_ == TeamSide::Home ? "home" : "away";
  return std::string("You are the head coach of the ") + side +
         " team in a live football match. Use the provided tools to inspect the "
         "match and to change your team's tactics. A read tool returns current "
         "values. A write tool only stages a change: it is validated and "
         "applied at the end of the turn, so never assume a change has taken "
         "effect inside the same turn. Make a small number of well-justified "
         "changes. When you are finished, reply with a short summary and no "
         "tool calls.";
}

void CoachSession::BeginTurn(const CoachTurnSnapshot& snapshot) {
  std::vector<AgentMessage> turn;
  AgentMessage user;
  user.role = AgentRole::User;
  user.content = "Observed match context (JSON):\n" +
                 CoachContextJson(snapshot.context).dump();
  turn.push_back(std::move(user));
  turns_.push_back(std::move(turn));
  while (turns_.size() > max_turns_) turns_.pop_front();
}

void CoachSession::Record(const AgentMessage& message) {
  if (turns_.empty()) turns_.emplace_back();
  turns_.back().push_back(message);
}

const std::vector<AgentMessage>& CoachSession::Messages() const {
  flat_.clear();
  flat_.push_back(system_);
  for (const std::vector<AgentMessage>& turn : turns_)
    flat_.insert(flat_.end(), turn.begin(), turn.end());
  return flat_;
}

void CoachSession::Reset() {
  turns_.clear();
  flat_.clear();
}

}  // namespace football::ai

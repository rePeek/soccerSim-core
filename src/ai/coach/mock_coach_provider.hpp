#ifndef FOOTBALL_AI_COACH_MOCK_COACH_PROVIDER_HPP
#define FOOTBALL_AI_COACH_MOCK_COACH_PROVIDER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "coach_model_provider.hpp"

namespace football::ai {

// Scripted in-process provider for loop tests and the async host's harness. It
// hands out queued replies in order and records every request, so a test can
// assert exactly what the model saw. With no script left it returns a benign
// final answer, so a loop always terminates instead of spinning.
class MockCoachProvider final : public ICoachModelProvider {
 public:
  void PushText(std::string content);
  void PushToolCalls(std::vector<AgentToolCall> tool_calls,
                     std::string content = {});
  void PushError(std::string error);
  void PushResponse(AgentModelResponse response);

  ModelCompletion Complete(const std::vector<AgentMessage>& messages) override;

  const std::vector<std::vector<AgentMessage>>& requests() const {
    return requests_;
  }
  std::size_t Remaining() const { return script_.size() - next_; }
  void Reset();

 private:
  struct Scripted {
    bool ok = true;
    AgentModelResponse response;
    std::string error;
  };

  std::vector<Scripted> script_;
  std::size_t next_ = 0;
  std::vector<std::vector<AgentMessage>> requests_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_MOCK_COACH_PROVIDER_HPP

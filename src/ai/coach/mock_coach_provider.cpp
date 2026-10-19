#include "mock_coach_provider.hpp"

#include <utility>

namespace football::ai {

void MockCoachProvider::PushText(std::string content) {
  AgentModelResponse response;
  response.message.role = AgentRole::Assistant;
  response.message.content = std::move(content);
  response.finish_reason = "stop";
  PushResponse(std::move(response));
}

void MockCoachProvider::PushToolCalls(std::vector<AgentToolCall> tool_calls,
                                      std::string content) {
  AgentModelResponse response;
  response.message.role = AgentRole::Assistant;
  response.message.content = std::move(content);
  response.message.tool_calls = std::move(tool_calls);
  response.finish_reason = "tool_calls";
  PushResponse(std::move(response));
}

void MockCoachProvider::PushError(std::string error) {
  Scripted scripted;
  scripted.ok = false;
  scripted.error = std::move(error);
  script_.push_back(std::move(scripted));
}

void MockCoachProvider::PushResponse(AgentModelResponse response) {
  Scripted scripted;
  scripted.ok = true;
  scripted.response = std::move(response);
  script_.push_back(std::move(scripted));
}

ModelCompletion MockCoachProvider::Complete(
    const std::vector<AgentMessage>& messages) {
  requests_.push_back(messages);
  ModelCompletion completion;
  if (next_ >= script_.size()) {
    completion.ok = true;
    completion.response.message.role = AgentRole::Assistant;
    completion.response.finish_reason = "stop";
    return completion;
  }
  const Scripted& scripted = script_[next_++];
  completion.ok = scripted.ok;
  completion.response = scripted.response;
  completion.error = scripted.error;
  return completion;
}

void MockCoachProvider::Reset() {
  script_.clear();
  next_ = 0;
  requests_.clear();
}

}  // namespace football::ai

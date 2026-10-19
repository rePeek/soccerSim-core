#include "mock_model_provider.hpp"

#include <utility>

namespace football::model {

void MockModelProvider::PushText(std::string content) {
  ModelResponse response;
  response.message.role = ModelRole::Assistant;
  response.message.content = std::move(content);
  response.finish_reason = "stop";
  PushResponse(std::move(response));
}

void MockModelProvider::PushToolCalls(std::vector<ModelToolCall> tool_calls,
                                      std::string content) {
  ModelResponse response;
  response.message.role = ModelRole::Assistant;
  response.message.content = std::move(content);
  response.message.tool_calls = std::move(tool_calls);
  response.finish_reason = "tool_calls";
  PushResponse(std::move(response));
}

void MockModelProvider::PushError(std::string error) {
  Scripted scripted;
  scripted.ok = false;
  scripted.error = std::move(error);
  script_.push_back(std::move(scripted));
}

void MockModelProvider::PushResponse(ModelResponse response) {
  Scripted scripted;
  scripted.ok = true;
  scripted.response = std::move(response);
  script_.push_back(std::move(scripted));
}

ModelCompletion MockModelProvider::Complete(const ModelRequest& request) {
  requests_.push_back(request);
  ModelCompletion completion;
  if (next_ >= script_.size()) {
    completion.ok = true;
    completion.response.message.role = ModelRole::Assistant;
    completion.response.finish_reason = "stop";
    return completion;
  }
  const Scripted& scripted = script_[next_++];
  completion.ok = scripted.ok;
  completion.response = scripted.response;
  completion.error = scripted.error;
  return completion;
}

void MockModelProvider::Reset() {
  script_.clear();
  next_ = 0;
  requests_.clear();
}

}  // namespace football::model

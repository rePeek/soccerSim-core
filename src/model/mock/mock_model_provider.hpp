#ifndef FOOTBALL_MODEL_MOCK_MODEL_PROVIDER_HPP
#define FOOTBALL_MODEL_MOCK_MODEL_PROVIDER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "football/model/model_provider.hpp"

namespace football::model {

// Scripted in-process provider for tests and the async host harness. It hands
// out queued replies in order and records every request, so a test can assert
// exactly what the model saw, including the tool definitions. With no script
// left it returns a benign final answer, so a caller always terminates instead
// of spinning.
class MockModelProvider final : public IModelProvider {
 public:
  void PushText(std::string content);
  void PushToolCalls(std::vector<ModelToolCall> tool_calls,
                     std::string content = {});
  void PushError(std::string error);
  void PushResponse(ModelResponse response);

  ModelCompletion Complete(const ModelRequest& request) override;

  const std::vector<ModelRequest>& requests() const { return requests_; }
  std::size_t Remaining() const { return script_.size() - next_; }
  void Reset();

 private:
  struct Scripted {
    bool ok = true;
    ModelResponse response;
    std::string error;
  };

  std::vector<Scripted> script_;
  std::size_t next_ = 0;
  std::vector<ModelRequest> requests_;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_MOCK_MODEL_PROVIDER_HPP

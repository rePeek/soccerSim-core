#ifndef FOOTBALL_AI_COACH_MODEL_PROVIDER_HPP
#define FOOTBALL_AI_COACH_MODEL_PROVIDER_HPP

#include <string>
#include <vector>

#include "agent_protocol.hpp"

namespace football::ai {

// The one blocking seam between the Agent Loop and a model. It is deliberately
// transport-free: the mock provider fills it directly and the OpenAI-compatible
// provider translates it to Chat Completions. Complete() may block on network
// I/O, so the loop only runs it on the agent's worker thread, never inside
// AIEngine::Update.
struct ModelCompletion {
  bool ok = false;
  AgentModelResponse response;
  std::string error;
};

class ICoachModelProvider {
 public:
  virtual ~ICoachModelProvider() = default;
  virtual ModelCompletion Complete(const std::vector<AgentMessage>& messages) = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_MODEL_PROVIDER_HPP

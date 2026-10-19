#ifndef FOOTBALL_MODEL_MODEL_PROVIDER_HPP
#define FOOTBALL_MODEL_MODEL_PROVIDER_HPP

#include <string>
#include <vector>

#include "football/model/model_protocol.hpp"

namespace football::model {

// One callable tool exposed to the model. `parameters_schema_json` is an opaque
// JSON Schema string: the model layer never interprets it, so this interface
// stays free of game types and of any JSON library type. The caller that owns a
// tool set builds it; a provider only forwards it to its endpoint.
struct ModelToolDefinition {
  std::string name;
  std::string description;
  std::string parameters_schema_json;
};

// Everything a provider needs for one completion: the conversation so far and
// the tools the model may call. The provider owns transport, auth, retries and
// timeouts; the caller owns the conversation and its domain state.
struct ModelRequest {
  std::vector<ModelMessage> messages;
  std::vector<ModelToolDefinition> tools;
};

struct ModelCompletion {
  bool ok = false;
  ModelResponse response;
  std::string error;
};

// The one blocking seam between a caller and a model. Complete() may block on
// network or inference I/O and must be called off the simulation thread. An
// implementation may be HTTP-backed, local or scripted; callers never know
// which, and they never link a transport they do not use.
class IModelProvider {
 public:
  virtual ~IModelProvider() = default;
  virtual ModelCompletion Complete(const ModelRequest& request) = 0;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_MODEL_PROVIDER_HPP

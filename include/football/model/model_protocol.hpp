#ifndef FOOTBALL_MODEL_MODEL_PROTOCOL_HPP
#define FOOTBALL_MODEL_MODEL_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace football::model {

// Provider-neutral model conversation protocol. It is the wire-independent
// language of any model caller: an HTTP-backed OpenAI-compatible provider
// translates it to Chat Completions, a local llama.cpp provider maps it to its
// own chat template, and a scripted test provider fills it directly. It carries
// no game or domain type, so it stays usable by every module that calls a model.

enum class ModelRole : std::uint8_t { System, User, Assistant, Tool };

// One model-requested tool invocation. `arguments_json` is the raw string the
// model produced; it is never trusted and is always decoded and validated by the
// caller. Assistant turns carry tool calls; every call gets exactly one tool
// message back, matched by `id`.
struct ModelToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
};

struct ModelMessage {
  ModelRole role = ModelRole::User;
  std::string content;
  std::vector<ModelToolCall> tool_calls;  // Assistant turns only.
  std::string tool_call_id;               // Tool turns only.
  std::string name;                       // Tool name on a tool turn.
};

// The full model reply for one completion: the assistant message plus
// informational accounting. `finish_reason` is provider prose, never a control
// signal the caller branches on.
struct ModelResponse {
  ModelMessage message;
  std::string finish_reason;
  std::uint32_t prompt_tokens = 0;
  std::uint32_t completion_tokens = 0;
};

// The outcome of executing one tool call. `content_json` is a structured payload
// serialised as a string (Chat Completions tool content is a string) and is fed
// back to the model as a tool message. A failed call still gets a message: the
// model must see the rejection to correct itself.
struct ModelToolResult {
  std::string tool_call_id;
  std::string name;
  bool ok = false;
  std::string content_json;

  ModelMessage ToMessage() const {
    ModelMessage message;
    message.role = ModelRole::Tool;
    message.name = name;
    message.tool_call_id = tool_call_id;
    message.content = content_json;
    return message;
  }
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_MODEL_PROTOCOL_HPP

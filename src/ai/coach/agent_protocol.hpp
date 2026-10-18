#ifndef FOOTBALL_AI_COACH_AGENT_PROTOCOL_HPP
#define FOOTBALL_AI_COACH_AGENT_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace football::ai {

// Provider-neutral agent conversation protocol. It is the wire-independent
// language of the Coach Agent Loop: the OpenAI-compatible provider translates
// it to and from Chat Completions, while a mock provider fills it directly and
// a test can assert on it without a socket. It carries no game type, so the
// loop and the protocol can be unit tested without a match.

enum class AgentRole : std::uint8_t { System, User, Assistant, Tool };

// One model-requested tool invocation. `arguments_json` is the raw string the
// model produced; it is never trusted and is always decoded and validated
// locally before it can reach a board. Assistant turns carry tool calls; every
// call gets exactly one tool message back, matched by `id`.
struct AgentToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
};

struct AgentMessage {
  AgentRole role = AgentRole::User;
  std::string content;
  std::vector<AgentToolCall> tool_calls;  // Assistant turns only.
  std::string tool_call_id;               // Tool turns only.
  std::string name;                       // Tool name on a tool turn.
};

// The full model reply for one completion: the assistant message plus
// informational accounting. `finish_reason` is provider prose, never a control
// signal the loop branches on.
struct AgentModelResponse {
  AgentMessage message;
  std::string finish_reason;
  std::uint32_t prompt_tokens = 0;
  std::uint32_t completion_tokens = 0;
};

// The outcome of executing one tool call. `content_json` is a structured
// payload serialised as a string (the OpenAI tool content is a string) and is
// fed back to the model as a tool message. A failed call still gets a message:
// the model must see the rejection to correct itself.
struct AgentToolResult {
  std::string tool_call_id;
  std::string name;
  bool ok = false;
  std::string content_json;

  AgentMessage ToMessage() const {
    AgentMessage message;
    message.role = AgentRole::Tool;
    message.name = name;
    message.tool_call_id = tool_call_id;
    message.content = content_json;
    return message;
  }
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_AGENT_PROTOCOL_HPP

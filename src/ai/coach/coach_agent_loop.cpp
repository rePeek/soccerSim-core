#include "coach_agent_loop.hpp"

#include <utility>

#include "coach_tool_decoder.hpp"
#include "coach_tool_executor.hpp"
#include "coach_tool_json.hpp"
#include "coach_tool_schema.hpp"

namespace football::ai {
namespace {

using nlohmann::json;

// A decode failure is fed back to the model as a rejected tool result so it can
// correct itself. It never reaches the board and never becomes a staged command.
std::string RejectionJson(CoachToolDecodeError error,
                          const std::string& message) {
  return json{{"status", "rejected"},
              {"error", std::string(DecodeErrorWireName(error))},
              {"message", message}}
      .dump();
}

std::string BudgetRejectionJson() {
  return json{{"status", "rejected"}, {"error", "tool_budget_exhausted"}}.dump();
}

}  // namespace

CoachAgentLoop::CoachAgentLoop(football::model::IModelProvider& provider,
                               CoachAgentConfig config)
    : provider_(provider), config_(config), tools_(CoachModelTools()) {}

CoachTurnResult CoachAgentLoop::Run(CoachSession& session,
                                    const CoachTurnSnapshot& snapshot) {
  CoachTurnResult result;
  session.BeginTurn(snapshot);

  for (int turn = 0; turn < config_.max_model_turns; ++turn) {
    football::model::ModelRequest request;
    request.messages = session.Messages();
    request.tools = tools_;
    const football::model::ModelCompletion completion =
        provider_.Complete(request);
    if (!completion.ok) {
      result.ok = false;
      result.error =
          completion.error.empty() ? "provider_error" : completion.error;
      result.model_turns = turn + 1;
      // The model never finalized, so an earlier partial proposal is void.
      result.staged.clear();
      return result;
    }
    ++result.model_turns;

    const football::model::ModelMessage assistant = completion.response.message;
    session.Record(assistant);
    result.transcript.push_back(assistant);

    // A reply without tool calls is the model's final answer for this turn.
    if (assistant.tool_calls.empty()) {
      result.ok = true;
      result.final_text = assistant.content;
      return result;
    }

    // Answer every tool call, even once the budget is spent, so the transcript
    // stays a valid assistant/tool pairing for the next turn.
    bool budget_exhausted = false;
    for (const football::model::ModelToolCall& call : assistant.tool_calls) {
      football::model::ModelToolResult tool_result;
      tool_result.tool_call_id = call.id;
      tool_result.name = call.name;

      if (result.tool_calls >= config_.max_tool_calls) {
        budget_exhausted = true;
        tool_result.ok = false;
        tool_result.content_json = BudgetRejectionJson();
      } else {
        const CoachToolDecodeResult decoded = CoachToolDecoder::DecodeJson(
            call.name, call.arguments_json, snapshot.view.self);
        if (!decoded.ok) {
          tool_result.ok = false;
          tool_result.content_json = RejectionJson(decoded.error, decoded.message);
        } else {
          CoachToolExecution execution =
              CoachToolExecutor::Execute(*decoded.request, snapshot.view);
          tool_result.ok = execution.ok;
          tool_result.content_json = std::move(execution.content_json);
          if (execution.command)
            result.staged.push_back({call.id, std::move(*execution.command)});
        }
        ++result.tool_calls;
      }

      const football::model::ModelMessage tool_message = tool_result.ToMessage();
      session.Record(tool_message);
      result.transcript.push_back(tool_message);
    }

    if (budget_exhausted) {
      // The model never saw every result, so the proposal is incomplete.
      result.ok = false;
      result.truncated = true;
      result.error = "tool_budget_exhausted";
      result.staged.clear();
      return result;
    }
  }

  // The model kept calling tools and never produced a final answer in budget.
  result.ok = false;
  result.truncated = true;
  result.error = "model_turn_budget_exhausted";
  result.staged.clear();
  return result;
}

}  // namespace football::ai

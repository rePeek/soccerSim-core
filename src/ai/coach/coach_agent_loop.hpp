#ifndef FOOTBALL_AI_COACH_AGENT_LOOP_HPP
#define FOOTBALL_AI_COACH_AGENT_LOOP_HPP

#include <string>
#include <vector>

#include "coach_session.hpp"
#include "football/ai/coach_command.hpp"
#include "football/model/model_provider.hpp"

namespace football::ai {

struct CoachAgentConfig {
  int max_model_turns = 4;
  int max_tool_calls = 8;
};

// A staged write and the tool call that produced it. `tool_call_id` lets the
// commit boundary and the replay log tie an applied command back to the model's
// request. Staging is not application: nothing here touched a board.
struct StagedCoachCommand {
  std::string tool_call_id;
  CoachCommand command;
};

struct CoachTurnResult {
  bool ok = false;         // the model produced a final answer within budget
  bool truncated = false;  // a budget or provider stop ended the turn early
  std::string final_text;  // the final assistant content, when ok
  std::string error;       // set when !ok
  // Complete, validated proposals for this turn. Cleared on truncation, because
  // an unfinished turn never commits. The commit boundary still re-validates
  // each command against the live board.
  std::vector<StagedCoachCommand> staged;
  int model_turns = 0;
  int tool_calls = 0;
  // This turn's assistant/tool messages, for logging and replay (AGENT-5).
  std::vector<football::model::ModelMessage> transcript;
};

// The provider-agnostic agent loop: model reply -> tool calls -> tool results ->
// continue reasoning, bounded by a model-turn and a tool-call budget. It builds
// a ModelRequest carrying the conversation and this team's tool definitions. It
// never applies a command; it collects staged proposals and returns them for the
// single commit boundary. It is synchronous and must run off the simulation
// thread, because IModelProvider::Complete() may block on the network.
class CoachAgentLoop {
 public:
  CoachAgentLoop(football::model::IModelProvider& provider,
                 CoachAgentConfig config = {});

  CoachTurnResult Run(CoachSession& session,
                      const CoachTurnSnapshot& snapshot);

 private:
  football::model::IModelProvider& provider_;
  CoachAgentConfig config_;
  // The coach tool set, built once from the registry/schema authority.
  std::vector<football::model::ModelToolDefinition> tools_;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_AGENT_LOOP_HPP

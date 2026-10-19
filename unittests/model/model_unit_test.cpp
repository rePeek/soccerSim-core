// AGENT-3A: the standalone football::model protocol and provider seam. These
// tests prove the mapping and the scripted provider without any AI dependency.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "football/model/model_protocol.hpp"
#include "football/model/model_provider.hpp"
#include "model/mock/mock_model_provider.hpp"

namespace {

using football::model::MockModelProvider;
using football::model::ModelMessage;
using football::model::ModelRequest;
using football::model::ModelResponse;
using football::model::ModelRole;
using football::model::ModelToolCall;
using football::model::ModelToolDefinition;
using football::model::ModelToolResult;

ModelToolCall Call(std::string id, std::string name, std::string arguments) {
  ModelToolCall call;
  call.id = std::move(id);
  call.name = std::move(name);
  call.arguments_json = std::move(arguments);
  return call;
}

}  // namespace

TEST_CASE("a tool result becomes a tool-role message",
          "[model][protocol]") {
  ModelToolResult result;
  result.tool_call_id = "call_1";
  result.name = "get_match_state";
  result.ok = true;
  result.content_json = R"({"tick":10})";

  const ModelMessage message = result.ToMessage();
  CHECK(message.role == ModelRole::Tool);
  CHECK(message.tool_call_id == "call_1");
  CHECK(message.name == "get_match_state");
  CHECK(message.content == R"({"tick":10})");
  CHECK(message.tool_calls.empty());
}

TEST_CASE("the mock provider scripts replies and records the full request",
          "[model][provider]") {
  MockModelProvider provider;
  provider.PushToolCalls({Call("c1", "get_match_state", "{}")});
  provider.PushText("done");

  ModelRequest request;
  request.messages.push_back(ModelMessage{ModelRole::User, "hello", {}, {}, {}});
  ModelToolDefinition definition;
  definition.name = "get_match_state";
  definition.description = "state";
  definition.parameters_schema_json = R"({"type":"object"})";
  request.tools.push_back(std::move(definition));

  const auto first = provider.Complete(request);
  REQUIRE(first.ok);
  REQUIRE(first.response.message.tool_calls.size() == 1);
  CHECK(first.response.message.tool_calls[0].id == "c1");
  CHECK(first.response.finish_reason == "tool_calls");

  const auto second = provider.Complete(request);
  REQUIRE(second.ok);
  CHECK(second.response.message.content == "done");
  CHECK(second.response.message.tool_calls.empty());

  REQUIRE(provider.requests().size() == 2);
  REQUIRE(provider.requests()[0].tools.size() == 1);
  CHECK(provider.requests()[0].tools[0].name == "get_match_state");
  CHECK(provider.requests()[0].tools[0].parameters_schema_json ==
        R"({"type":"object"})");
  CHECK(provider.requests()[0].messages.size() == 1);
  CHECK(provider.Remaining() == 0);
}

TEST_CASE("the mock provider surfaces errors and terminates when unscripted",
          "[model][provider]") {
  MockModelProvider provider;
  provider.PushError("boom");
  const ModelRequest request;

  const auto failed = provider.Complete(request);
  CHECK_FALSE(failed.ok);
  CHECK(failed.error == "boom");

  // No script left: a benign final answer, so a caller never spins.
  const auto fallback = provider.Complete(request);
  REQUIRE(fallback.ok);
  CHECK(fallback.response.message.role == ModelRole::Assistant);
  CHECK(fallback.response.message.tool_calls.empty());

  provider.Reset();
  CHECK(provider.Remaining() == 0);
  CHECK(provider.requests().empty());
}

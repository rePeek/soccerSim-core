// AGENT-1: the provider-neutral agent protocol, the wire-name tables and the
// OpenAI-compatible JSON Schema for the nine coach tools.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ai/coach/agent_protocol.hpp"
#include "ai/coach/coach_tool_names.hpp"
#include "ai/coach/coach_tool_schema.hpp"
#include "ai/coach/tool_registry.hpp"

namespace {

using football::ai::AgentMessage;
using football::ai::AgentRole;
using football::ai::AgentToolResult;
using football::ai::BuildUpStyle;
using football::ai::CoachToolList;
using football::ai::CoachToolParameters;
using football::ai::CoachToolRegistry;
using football::ai::DefendingRole;
using football::ai::IndividualKey;
using football::ai::InstructionKey;
using football::ai::MarkingStyle;
using football::ai::Mentality;
using football::ai::PossessionRole;
using football::ai::ShoutType;
using football::ai::TacticalPhase;
using football::ai::TransitionKey;
using football::ai::TransitionOnLoss;
using football::ai::TransitionOnWin;

template <typename E, typename ToName, typename ParseFn>
void CheckRoundTrip(std::size_t count, ToName to_name, ParseFn parse) {
  for (std::size_t i = 0; i < count; ++i) {
    const E value = static_cast<E>(i);
    const std::string_view name = to_name(value);
    CHECK_FALSE(name.empty());
    const std::optional<E> parsed = parse(name);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == value);
  }
}

}  // namespace

TEST_CASE("a tool result becomes a tool-role message", "[ai][coach][agent]") {
  AgentToolResult result;
  result.tool_call_id = "call_1";
  result.name = "get_match_state";
  result.ok = true;
  result.content_json = R"({"tick":10})";

  const AgentMessage message = result.ToMessage();
  CHECK(message.role == AgentRole::Tool);
  CHECK(message.tool_call_id == "call_1");
  CHECK(message.name == "get_match_state");
  CHECK(message.content == R"({"tick":10})");
  CHECK(message.tool_calls.empty());
}

TEST_CASE("every tool enum name round-trips through the wire tables",
          "[ai][coach][agent][names]") {
  CheckRoundTrip<InstructionKey>(15, [](InstructionKey v) { return football::ai::ToWireName(v); },
                                 [](std::string_view n) { return football::ai::ParseInstructionKey(n); });
  CheckRoundTrip<IndividualKey>(6, [](IndividualKey v) { return football::ai::ToWireName(v); },
                                [](std::string_view n) { return football::ai::ParseIndividualKey(n); });
  CheckRoundTrip<TransitionKey>(2, [](TransitionKey v) { return football::ai::ToWireName(v); },
                                [](std::string_view n) { return football::ai::ParseTransitionKey(n); });
  CheckRoundTrip<Mentality>(7, [](Mentality v) { return football::ai::ToWireName(v); },
                            [](std::string_view n) { return football::ai::ParseMentality(n); });
  CheckRoundTrip<BuildUpStyle>(3, [](BuildUpStyle v) { return football::ai::ToWireName(v); },
                               [](std::string_view n) { return football::ai::ParseBuildUpStyle(n); });
  CheckRoundTrip<MarkingStyle>(3, [](MarkingStyle v) { return football::ai::ToWireName(v); },
                               [](std::string_view n) { return football::ai::ParseMarkingStyle(n); });
  CheckRoundTrip<PossessionRole>(11, [](PossessionRole v) { return football::ai::ToWireName(v); },
                                 [](std::string_view n) { return football::ai::ParsePossessionRole(n); });
  CheckRoundTrip<DefendingRole>(8, [](DefendingRole v) { return football::ai::ToWireName(v); },
                                [](std::string_view n) { return football::ai::ParseDefendingRole(n); });
  CheckRoundTrip<TransitionOnWin>(3, [](TransitionOnWin v) { return football::ai::ToWireName(v); },
                                  [](std::string_view n) { return football::ai::ParseTransitionOnWin(n); });
  CheckRoundTrip<TransitionOnLoss>(3, [](TransitionOnLoss v) { return football::ai::ToWireName(v); },
                                   [](std::string_view n) { return football::ai::ParseTransitionOnLoss(n); });
  CheckRoundTrip<TacticalPhase>(2, [](TacticalPhase v) { return football::ai::ToWireName(v); },
                                [](std::string_view n) { return football::ai::ParseTacticalPhase(n); });
  CheckRoundTrip<ShoutType>(8, [](ShoutType v) { return football::ai::ToWireName(v); },
                            [](std::string_view n) { return football::ai::ParseShoutType(n); });
}

TEST_CASE("the tool list covers every registry tool with an object schema",
          "[ai][coach][agent][schema]") {
  const auto list = CoachToolList(false);
  REQUIRE(list.size() == CoachToolRegistry::Tools().size());
  REQUIRE(list.size() == 9);

  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& entry = list[i];
    CHECK(entry.at("type") == "function");
    const auto& function = entry.at("function");
    CHECK(function.at("name") == CoachToolRegistry::Tools()[i].name);
    CHECK_FALSE(function.at("description").get<std::string>().empty());
    CHECK_FALSE(function.contains("strict"));

    const auto& parameters = function.at("parameters");
    CHECK(parameters.at("type") == "object");
    CHECK(parameters.at("additionalProperties") == false);
    CHECK(parameters.contains("properties"));
    CHECK(parameters.contains("required"));
  }

  const auto strict = CoachToolList(true);
  for (const auto& entry : strict) CHECK(entry.at("function").at("strict") == true);
}

TEST_CASE("the schema types each instruction value instead of accepting any value",
          "[ai][coach][agent][schema]") {
  const auto parameters = CoachToolParameters("set_team_instruction");
  CHECK(parameters.at("type") == "object");
  REQUIRE(parameters.at("required").size() == 2);

  const auto& key = parameters.at("properties").at("key");
  CHECK(key.at("type") == "string");
  CHECK(key.at("enum").size() == 15);

  const auto& value = parameters.at("properties").at("value");
  REQUIRE(value.contains("oneOf"));
  REQUIRE(value.at("oneOf").size() == 3);
  CHECK(value.at("oneOf")[0].at("type") == "number");
  CHECK(value.at("oneOf")[0].at("minimum") == 0.0);
  CHECK(value.at("oneOf")[0].at("maximum") == 1.0);
  CHECK(value.at("oneOf")[1].at("type") == "string");
  CHECK(value.at("oneOf")[2].at("type") == "boolean");
}

TEST_CASE("scalar, array and id schemas carry their bounds",
          "[ai][coach][agent][schema]") {
  const auto anchor = CoachToolParameters("set_slot_anchor");
  CHECK(anchor.at("properties").at("slot").at("type") == "integer");
  CHECK(anchor.at("properties").at("slot").at("maximum") == 10);
  CHECK(anchor.at("properties").at("phase").at("enum").size() == 2);
  CHECK(anchor.at("properties").at("u").at("minimum") == 0.0);
  CHECK(anchor.at("properties").at("v").at("minimum") == -1.0);

  const auto shout = CoachToolParameters("shout_to_players");
  const auto& targets = shout.at("properties").at("targets");
  CHECK(targets.at("type") == "array");
  CHECK(targets.at("minItems") == 1);
  CHECK(targets.at("maxItems") == 11);
  CHECK(targets.at("items").at("type") == "integer");

  const auto individual = CoachToolParameters("set_individual_instruction");
  REQUIRE(individual.at("properties").at("value").at("oneOf").size() == 2);
  CHECK(individual.at("properties").at("value").at("oneOf")[1].at("type") == "integer");
}

TEST_CASE("an unknown tool has no parameters", "[ai][coach][agent][schema]") {
  CHECK(CoachToolParameters("does_not_exist").empty());
}

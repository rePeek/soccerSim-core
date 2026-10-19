#include "coach_tool_schema.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "coach_tool_names.hpp"
#include "football/ai/coach_command.hpp"
#include "tool_registry.hpp"

namespace football::ai {
namespace {

using nlohmann::json;

// kInvalidPlayerId is the sentinel; the largest addressable id is one below it.
constexpr std::int64_t kMaxPlayerId =
    static_cast<std::int64_t>(football::player::kInvalidPlayerId) - 1;

// Every edit field is bounded: the decoder re-checks, but a model that reads
// the schema should not have to guess the legal range.
constexpr std::int64_t kMaxSlot = static_cast<std::int64_t>(kTacticalSlotCount) - 1;

json NumberSchema(double low, double high) {
  json schema;
  schema["type"] = "number";
  schema["minimum"] = low;
  schema["maximum"] = high;
  return schema;
}

json IntegerSchema(std::int64_t low, std::int64_t high) {
  json schema;
  schema["type"] = "integer";
  schema["minimum"] = low;
  schema["maximum"] = high;
  return schema;
}

json BooleanSchema() {
  json schema;
  schema["type"] = "boolean";
  return schema;
}

json StringEnumSchema(json names) {
  json schema;
  schema["type"] = "string";
  schema["enum"] = std::move(names);
  return schema;
}

json ObjectSchema(json properties, std::initializer_list<const char*> required) {
  json schema;
  schema["type"] = "object";
  schema["properties"] = std::move(properties);
  schema["required"] = json::array();
  for (const char* key : required) schema["required"].push_back(key);
  // Local decoding rejects unknown fields too; the schema states it up front.
  schema["additionalProperties"] = false;
  return schema;
}

template <typename E, typename ToName>
json EnumNames(std::size_t count, ToName to_name) {
  json names = json::array();
  for (std::size_t i = 0; i < count; ++i)
    names.push_back(std::string(to_name(static_cast<E>(i))));
  return names;
}

// Concatenates enum name lists, dropping duplicates that appear in more than
// one C++ enum (e.g. "balanced" and "goalkeeper"). The tool's key/phase field
// disambiguates which enum a name belongs to; the decoder enforces that.
json UniqueConcat(std::initializer_list<json> lists) {
  std::vector<std::string> seen;
  json names = json::array();
  for (const json& list : lists) {
    for (const json& entry : list) {
      const std::string name = entry.get<std::string>();
      bool duplicate = false;
      for (const std::string& present : seen) {
        if (present == name) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      seen.push_back(name);
      names.push_back(name);
    }
  }
  return names;
}

json InstructionKeyNames() {
  return EnumNames<InstructionKey>(15, [](InstructionKey key) {
    return ToWireName(key);
  });
}

json IndividualKeyNames() {
  return EnumNames<IndividualKey>(6, [](IndividualKey key) {
    return ToWireName(key);
  });
}

json TeamInstructionParameters() {
  json properties = json::object();
  properties["key"] = StringEnumSchema(InstructionKeyNames());

  json value;
  value["description"] =
      "A number in [0,1] for the scalar keys, a boolean for pass_into_space "
      "and offside_trap, or the matching name for mentality, build_up and "
      "marking. The accepted type follows key.";
  value["oneOf"] = json::array(
      {NumberSchema(0.0, 1.0),
       StringEnumSchema(UniqueConcat(
           {EnumNames<Mentality>(7, [](Mentality v) { return ToWireName(v); }),
            EnumNames<BuildUpStyle>(
                3, [](BuildUpStyle v) { return ToWireName(v); }),
            EnumNames<MarkingStyle>(
                3, [](MarkingStyle v) { return ToWireName(v); })})),
       BooleanSchema()});
  properties["value"] = std::move(value);
  return ObjectSchema(std::move(properties), {"key", "value"});
}

json SlotRoleParameters() {
  json properties = json::object();
  properties["slot"] = IntegerSchema(0, kMaxSlot);
  properties["phase"] = StringEnumSchema(
      EnumNames<TacticalPhase>(2, [](TacticalPhase v) { return ToWireName(v); }));
  // Both role families are listed; the decoder accepts only the family that
  // matches the phase.
  properties["role"] = StringEnumSchema(UniqueConcat(
      {EnumNames<PossessionRole>(
           11, [](PossessionRole v) { return ToWireName(v); }),
       EnumNames<DefendingRole>(
           8, [](DefendingRole v) { return ToWireName(v); })}));
  return ObjectSchema(std::move(properties), {"slot", "phase", "role"});
}

json SlotAnchorParameters() {
  json properties = json::object();
  properties["slot"] = IntegerSchema(0, kMaxSlot);
  properties["phase"] = StringEnumSchema(
      EnumNames<TacticalPhase>(2, [](TacticalPhase v) { return ToWireName(v); }));
  properties["u"] = NumberSchema(0.0, 1.0);
  properties["v"] = NumberSchema(-1.0, 1.0);
  return ObjectSchema(std::move(properties), {"slot", "phase", "u", "v"});
}

json IndividualInstructionParameters() {
  json properties = json::object();
  properties["slot"] = IntegerSchema(0, kMaxSlot);
  properties["key"] = StringEnumSchema(IndividualKeyNames());

  json value;
  value["description"] =
      "A number in [0,1] for the scalar keys, or an observed opponent player "
      "id for marking_target.";
  value["oneOf"] = json::array({NumberSchema(0.0, 1.0),
                                IntegerSchema(1, kMaxPlayerId)});
  properties["value"] = std::move(value);
  return ObjectSchema(std::move(properties), {"slot", "key", "value"});
}

json TransitionInstructionParameters() {
  json properties = json::object();
  properties["key"] = StringEnumSchema(
      EnumNames<TransitionKey>(2, [](TransitionKey key) { return ToWireName(key); }));
  properties["value"] = StringEnumSchema(UniqueConcat(
      {EnumNames<TransitionOnWin>(
           3, [](TransitionOnWin v) { return ToWireName(v); }),
       EnumNames<TransitionOnLoss>(
           3, [](TransitionOnLoss v) { return ToWireName(v); })}));
  return ObjectSchema(std::move(properties), {"key", "value"});
}

json ShoutParameters() {
  json properties = json::object();
  properties["type"] = StringEnumSchema(
      EnumNames<ShoutType>(8, [](ShoutType v) { return ToWireName(v); }));

  json targets;
  targets["type"] = "array";
  targets["description"] = "Own, currently observed players to shout at.";
  targets["items"] = IntegerSchema(1, kMaxPlayerId);
  targets["minItems"] = 1;
  targets["maxItems"] = kMaxShoutTargets;
  properties["targets"] = std::move(targets);
  return ObjectSchema(std::move(properties), {"type", "targets"});
}

}  // namespace

nlohmann::json CoachToolParameters(std::string_view tool_name) {
  if (tool_name == "get_match_state") return ObjectSchema(json::object(), {});
  if (tool_name == "get_tactical_board") return ObjectSchema(json::object(), {});
  if (tool_name == "get_player_info") {
    json properties = json::object();
    properties["player"] = IntegerSchema(1, kMaxPlayerId);
    return ObjectSchema(std::move(properties), {"player"});
  }
  if (tool_name == "set_team_instruction") return TeamInstructionParameters();
  if (tool_name == "set_slot_role") return SlotRoleParameters();
  if (tool_name == "set_slot_anchor") return SlotAnchorParameters();
  if (tool_name == "set_individual_instruction")
    return IndividualInstructionParameters();
  if (tool_name == "set_transition_instruction")
    return TransitionInstructionParameters();
  if (tool_name == "shout_to_players") return ShoutParameters();
  return json::object();
}

nlohmann::json CoachToolList(bool strict) {
  json list = json::array();
  for (const CoachToolDescriptor& tool : CoachToolRegistry::Tools()) {
    json function;
    function["name"] = std::string(tool.name);
    function["description"] = std::string(tool.summary);
    function["parameters"] = CoachToolParameters(tool.name);
    if (strict) function["strict"] = true;
    json entry;
    entry["type"] = "function";
    entry["function"] = std::move(function);
    list.push_back(std::move(entry));
  }
  return list;
}

std::vector<football::model::ModelToolDefinition> CoachModelTools() {
  std::vector<football::model::ModelToolDefinition> tools;
  tools.reserve(CoachToolRegistry::Tools().size());
  for (const CoachToolDescriptor& tool : CoachToolRegistry::Tools()) {
    football::model::ModelToolDefinition definition;
    definition.name = std::string(tool.name);
    definition.description = std::string(tool.summary);
    definition.parameters_schema_json = CoachToolParameters(tool.name).dump();
    tools.push_back(std::move(definition));
  }
  return tools;
}

}  // namespace football::ai

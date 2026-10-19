#ifndef FOOTBALL_AI_COACH_TOOL_SCHEMA_HPP
#define FOOTBALL_AI_COACH_TOOL_SCHEMA_HPP

#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "football/model/model_provider.hpp"

namespace football::ai {

// JSON Schema for the coach tool set, in the OpenAI-compatible function-calling
// subset (type/properties/required/enum/minimum/maximum/items/oneOf). The tool
// registry stays the single authority for names, mutating flags and summaries;
// these functions only attach parameters, so the advertised schema cannot name
// a tool the decoder does not know.
//
// `strict` adds the OpenAI structured-output `strict: true` marker. It is a
// per-endpoint capability: a compatible endpoint that rejects strict mode is
// configured non-strict. Either way, the full validation is local, in
// CoachToolDecoder, because a model's arguments are never trusted.
nlohmann::json CoachToolParameters(std::string_view tool_name);

// The registry's nine tools as provider-neutral definitions, ready to attach
// to a ModelRequest. `parameters_schema_json` is the JSON Schema for that tool;
// the model layer never parses it.
std::vector<football::model::ModelToolDefinition> CoachModelTools();

// Ready for the Chat Completions `tools` array.
nlohmann::json CoachToolList(bool strict = false);

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_SCHEMA_HPP

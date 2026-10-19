#ifndef FOOTBALL_AI_COACH_TOOL_JSON_HPP
#define FOOTBALL_AI_COACH_TOOL_JSON_HPP

#include <string_view>

#include <nlohmann/json.hpp>

#include "coach_tool_decoder.hpp"
#include "football/ai/coach_context.hpp"
#include "football/ai/tactical_plan.hpp"
#include "tool_registry.hpp"

namespace football::ai {

// Structured JSON for everything the agent sends or receives: tool results, the
// turn's context message and the wire names for the read-payload enums. Keeping
// it in one place means a read tool and the context message describe the same
// world with the same vocabulary. It is internal to football_ai and never a
// public contract.

nlohmann::json MatchStateJson(const CoachMatchStateSummary& summary);
nlohmann::json TacticalPlanJson(const TacticalPlanV2& plan);
nlohmann::json PlayerInfoJson(const CoachPlayerInfo& info);
nlohmann::json CoachContextJson(const CoachContext& context);

std::string_view PhaseWireName(MatchPhase phase);
std::string_view RoleWireName(e_PlayerRole role);
std::string_view CoachEventWireName(CoachEventType type);
std::string_view DecodeErrorWireName(CoachToolDecodeError error);

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_JSON_HPP

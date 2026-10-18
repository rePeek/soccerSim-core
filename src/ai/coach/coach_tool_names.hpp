#ifndef FOOTBALL_AI_COACH_TOOL_NAMES_HPP
#define FOOTBALL_AI_COACH_TOOL_NAMES_HPP

#include <cstddef>
#include <optional>
#include <string_view>

#include "football/ai/coach_command.hpp"
#include "football/ai/tactical_plan.hpp"

namespace football::ai {

// Canonical wire names for coach tool arguments. One table per enum, exposed as
// a name/parse pair, is the single authority the JSON schema and the decoder
// are both generated from, so the advertised schema and the local validation
// cannot drift apart. Names are lower_snake_case.

// The value alternative a SetTeamInstruction key accepts. The decoder rejects
// a value whose JSON type does not match the key.
enum class InstructionValueKind : std::uint8_t {
  Number,
  Boolean,
  Mentality,
  BuildUpStyle,
  MarkingStyle,
};

// The value alternative a SetIndividualInstruction key accepts.
enum class IndividualValueKind : std::uint8_t { Number, MarkingTarget };

// Upper bound on shout targets in one call: a full side. The runtime still
// enforces its own per-player shout lifecycle.
inline constexpr std::size_t kMaxShoutTargets = 11;

std::string_view ToWireName(InstructionKey key);
std::optional<InstructionKey> ParseInstructionKey(std::string_view name);
InstructionValueKind KindOf(InstructionKey key);

std::string_view ToWireName(IndividualKey key);
std::optional<IndividualKey> ParseIndividualKey(std::string_view name);
IndividualValueKind KindOf(IndividualKey key);

std::string_view ToWireName(TransitionKey key);
std::optional<TransitionKey> ParseTransitionKey(std::string_view name);

std::string_view ToWireName(Mentality value);
std::optional<Mentality> ParseMentality(std::string_view name);

std::string_view ToWireName(BuildUpStyle value);
std::optional<BuildUpStyle> ParseBuildUpStyle(std::string_view name);

std::string_view ToWireName(MarkingStyle value);
std::optional<MarkingStyle> ParseMarkingStyle(std::string_view name);

std::string_view ToWireName(PossessionRole value);
std::optional<PossessionRole> ParsePossessionRole(std::string_view name);

std::string_view ToWireName(DefendingRole value);
std::optional<DefendingRole> ParseDefendingRole(std::string_view name);

std::string_view ToWireName(TransitionOnWin value);
std::optional<TransitionOnWin> ParseTransitionOnWin(std::string_view name);

std::string_view ToWireName(TransitionOnLoss value);
std::optional<TransitionOnLoss> ParseTransitionOnLoss(std::string_view name);

std::string_view ToWireName(TacticalPhase value);
std::optional<TacticalPhase> ParseTacticalPhase(std::string_view name);

std::string_view ToWireName(ShoutType value);
std::optional<ShoutType> ParseShoutType(std::string_view name);

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_TOOL_NAMES_HPP

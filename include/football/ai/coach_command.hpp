#ifndef FOOTBALL_AI_COACH_COMMAND_HPP
#define FOOTBALL_AI_COACH_COMMAND_HPP

#include <cstdint>
#include <variant>
#include <vector>

#include "football/ai/tactical_board.hpp"
#include "football/ai/tactical_plan.hpp"
#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"

namespace football::ai {

// Typed coach commands. A future LLM client maps JSON tool_calls onto these
// strongly-typed values at the provider boundary; the core never parses free
// text and never accepts an unvalidated board overwrite.

enum class ShoutType : std::uint8_t {
  Encourage,
  Praise,
  DemandMore,
  FireUp,
  Berate,
  Focus,
  CalmDown,
  NoPressure,
};

// Team-level instruction keys. Each key accepts exactly one value alternative.
enum class InstructionKey : std::uint8_t {
  Mentality,
  AttackingWidth,
  Tempo,
  PassingDirectness,
  RiskTaking,
  CreativeFreedom,
  AttackingBias,
  BuildUp,
  PassIntoSpace,
  DefensiveLine,
  LineOfEngagement,
  PressingIntensity,
  Compactness,
  Marking,
  OffsideTrap,
};

// Number for the scalar keys, the enum/bool for the categorical keys.
using InstructionValue =
    std::variant<float, bool, Mentality, BuildUpStyle, MarkingStyle>;

enum class IndividualKey : std::uint8_t {
  ForwardRuns,
  Pressing,
  PassingRisk,
  Dribbling,
  PositionalFreedom,
  MarkingTarget,
};

using IndividualValue = std::variant<float, football::player::PlayerId>;

enum class TransitionKey : std::uint8_t { OnWin, OnLoss };
using TransitionValue = std::variant<TransitionOnWin, TransitionOnLoss>;

// A role is either an in-possession or an out-of-possession role; the phase on
// SetSlotRole decides which alternative is legal.
using RoleId = std::variant<PossessionRole, DefendingRole>;

struct SetTeamInstruction {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  InstructionKey key = InstructionKey::Mentality;
  InstructionValue value;
};

struct SetSlotRole {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  TacticalSlotId slot = 0;
  TacticalPhase phase = TacticalPhase::InPossession;
  RoleId role;
};

struct SetSlotAnchor {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  TacticalSlotId slot = 0;
  TacticalPhase phase = TacticalPhase::InPossession;
  TacticalAnchor anchor;
};

struct SetIndividualInstruction {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  TacticalSlotId slot = 0;
  IndividualKey key = IndividualKey::ForwardRuns;
  IndividualValue value;
};

struct SetTransitionInstruction {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  TransitionKey key = TransitionKey::OnWin;
  TransitionValue value;
};

struct IssueShout {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  ShoutType type = ShoutType::Encourage;
  std::vector<football::player::PlayerId> targets;
};

using CoachCommand =
    std::variant<SetTeamInstruction, SetSlotRole, SetSlotAnchor,
                 SetIndividualInstruction, SetTransitionInstruction,
                 IssueShout>;

// The side a command targets. Never reads another side's data.
football::sim::TeamSide CoachCommandSide(const CoachCommand& command);

enum class CoachErrorCode : std::uint8_t {
  Ok = 0,
  StaleRevision,
  Expired,
  FutureTick,
  DuplicateId,
  InvalidPhase,
  LegacyBoard,
  InvalidSlot,
  InvalidEnum,
  NonFiniteValue,
  ValueOutOfRange,
  UnknownPlayer,
  WrongTeam,
  InvalidMarkingTarget,
  Unsupported,
  MalformedCommand,
  NoShoutTargets,
  ShoutCooldown,
};

struct CoachCommandEnvelope {
  std::uint64_t id = 0;                     // per-match/per-side idempotency key
  std::uint64_t observed_tick = 0;
  std::uint64_t expires_at_tick = 0;
  std::uint64_t expected_board_revision = 0;
  CoachCommand command;
};

struct CoachCommandResult {
  bool accepted = false;
  CoachErrorCode error = CoachErrorCode::Ok;
  const char* message = "";
  // Applied tick is the commit tick; on failure it stays 0 and nothing moved.
  std::uint64_t applied_tick = 0;
  std::uint64_t new_board_revision = 0;

  explicit operator bool() const { return accepted; }
};

// Append-only, replayable record. Pure data; no database required.
struct CoachCommandRecord {
  std::uint64_t match_instance_id = 0;
  std::uint64_t call_id = 0;
  std::uint64_t observed_tick = 0;
  std::uint64_t applied_tick = 0;
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  std::uint64_t before_revision = 0;
  std::uint64_t after_revision = 0;
  CoachCommand command;
  CoachCommandResult result;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_COMMAND_HPP

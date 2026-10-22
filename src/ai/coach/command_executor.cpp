#include "command_executor.hpp"

#include <cmath>
#include <optional>

namespace football::ai {
namespace {
using football::player::PlayerId;

CoachCommandResult Reject(CoachErrorCode code, const char* message) {
  CoachCommandResult result;
  result.accepted = false;
  result.error = code;
  result.message = message;
  return result;
}

bool FiniteInRange(float value, float low, float high) {
  return std::isfinite(value) && value >= low && value <= high;
}

const WorldPlayerState* FindWorldPlayer(const WorldState& world, PlayerId id) {
  for (const WorldPlayerState& player : world.players)
    if (player.id == id) return &player;
  return nullptr;
}

std::optional<TacticalSlot*> SlotOf(TacticalPlan& plan, TacticalSlotId slot) {
  if (slot >= kTacticalSlotCount) return std::nullopt;
  for (TacticalSlot& candidate : plan.slots)
    if (candidate.id == slot) return &candidate;
  return std::nullopt;
}

// Applies the command to a plan copy. Returns nullopt when the command is valid.
std::optional<CoachCommandResult> ApplyToPlan(TacticalPlan& plan,
                                              const CoachCommand& command,
                                              const WorldState& world) {
  if (const auto* set = std::get_if<SetTeamInstruction>(&command)) {
    switch (set->key) {
      case InstructionKey::Mentality: {
        const auto* mentality = std::get_if<Mentality>(&set->value);
        if (mentality == nullptr) return Reject(CoachErrorCode::MalformedCommand, "mentality expects a Mentality");
        if (static_cast<std::size_t>(*mentality) >= 7) return Reject(CoachErrorCode::InvalidEnum, "invalid mentality");
        // Mentality is the preset source: it re-applies the preset defaults.
        plan.mentality = *mentality;
        plan.in_possession = DefaultInPossession(*mentality);
        plan.out_of_possession = DefaultOutOfPossession(*mentality);
        return std::nullopt;
      }
      case InstructionKey::BuildUp: {
        const auto* value = std::get_if<BuildUpStyle>(&set->value);
        if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "build_up expects a style");
        if (static_cast<std::size_t>(*value) >= 3) return Reject(CoachErrorCode::InvalidEnum, "invalid build-up");
        plan.in_possession.build_up = *value;
        return std::nullopt;
      }
      case InstructionKey::Marking: {
        const auto* value = std::get_if<MarkingStyle>(&set->value);
        if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "marking expects a style");
        if (static_cast<std::size_t>(*value) >= 3) return Reject(CoachErrorCode::InvalidEnum, "invalid marking");
        plan.out_of_possession.marking = *value;
        return std::nullopt;
      }
      case InstructionKey::PassIntoSpace: {
        const auto* value = std::get_if<bool>(&set->value);
        if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "pass_into_space expects a bool");
        plan.in_possession.pass_into_space = *value;
        return std::nullopt;
      }
      case InstructionKey::OffsideTrap: {
        const auto* value = std::get_if<bool>(&set->value);
        if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "offside_trap expects a bool");
        plan.out_of_possession.offside_trap = *value;
        return std::nullopt;
      }
      default: break;
    }
    const auto* number = std::get_if<float>(&set->value);
    if (number == nullptr) return Reject(CoachErrorCode::MalformedCommand, "scalar instruction expects a number");
    if (!std::isfinite(*number)) return Reject(CoachErrorCode::NonFiniteValue, "non-finite value");
    if (*number < 0.0f || *number > 1.0f) return Reject(CoachErrorCode::ValueOutOfRange, "value out of [0,1]");
    switch (set->key) {
      case InstructionKey::AttackingWidth: plan.in_possession.attacking_width = *number; break;
      case InstructionKey::Tempo: plan.in_possession.tempo = *number; break;
      case InstructionKey::PassingDirectness: plan.in_possession.passing_directness = *number; break;
      case InstructionKey::RiskTaking: plan.in_possession.risk_taking = *number; break;
      case InstructionKey::CreativeFreedom: plan.in_possession.creative_freedom = *number; break;
      case InstructionKey::AttackingBias: plan.in_possession.attacking_bias = *number; break;
      case InstructionKey::DefensiveLine: plan.out_of_possession.defensive_line = *number; break;
      case InstructionKey::LineOfEngagement: plan.out_of_possession.line_of_engagement = *number; break;
      case InstructionKey::PressingIntensity: plan.out_of_possession.pressing_intensity = *number; break;
      case InstructionKey::Compactness: plan.out_of_possession.compactness = *number; break;
      default: return Reject(CoachErrorCode::MalformedCommand, "instruction key does not accept a number");
    }
    return std::nullopt;
  }

  if (const auto* set = std::get_if<SetSlotRole>(&command)) {
    auto slot = SlotOf(plan, set->slot);
    if (!slot) return Reject(CoachErrorCode::InvalidSlot, "slot out of range");
    if (set->phase == TacticalPhase::InPossession) {
      const auto* role = std::get_if<PossessionRole>(&set->role);
      if (role == nullptr) return Reject(CoachErrorCode::MalformedCommand, "in-possession role expected");
      if (static_cast<std::size_t>(*role) >= 11) return Reject(CoachErrorCode::InvalidEnum, "invalid role");
      (*slot)->in_possession_role = *role;
    } else {
      const auto* role = std::get_if<DefendingRole>(&set->role);
      if (role == nullptr) return Reject(CoachErrorCode::MalformedCommand, "out-of-possession role expected");
      if (static_cast<std::size_t>(*role) >= 8) return Reject(CoachErrorCode::InvalidEnum, "invalid role");
      (*slot)->out_of_possession_role = *role;
    }
    return std::nullopt;
  }

  if (const auto* set = std::get_if<SetSlotAnchor>(&command)) {
    auto slot = SlotOf(plan, set->slot);
    if (!slot) return Reject(CoachErrorCode::InvalidSlot, "slot out of range");
    if (!std::isfinite(set->anchor.u) || !std::isfinite(set->anchor.v))
      return Reject(CoachErrorCode::NonFiniteValue, "non-finite anchor");
    if (set->anchor.u < 0.0f || set->anchor.u > 1.0f || set->anchor.v < -1.0f ||
        set->anchor.v > 1.0f)
      return Reject(CoachErrorCode::ValueOutOfRange, "anchor out of range");
    if (set->phase == TacticalPhase::InPossession)
      (*slot)->in_possession = set->anchor;
    else
      (*slot)->out_of_possession = set->anchor;
    return std::nullopt;
  }

  if (const auto* set = std::get_if<SetIndividualInstruction>(&command)) {
    auto slot = SlotOf(plan, set->slot);
    if (!slot) return Reject(CoachErrorCode::InvalidSlot, "slot out of range");
    IndividualInstructions& instructions = (*slot)->instructions;
    if (set->key == IndividualKey::MarkingTarget) {
      const auto* target = std::get_if<PlayerId>(&set->value);
      if (target == nullptr) return Reject(CoachErrorCode::MalformedCommand, "marking target expects a PlayerId");
      if (*target == football::player::kInvalidPlayerId)
        return Reject(CoachErrorCode::InvalidMarkingTarget, "invalid PlayerId");
      const WorldPlayerState* opponent = FindWorldPlayer(world, *target);
      if (opponent == nullptr) return Reject(CoachErrorCode::UnknownPlayer, "marking target not observed");
      if (opponent->side == set->side) return Reject(CoachErrorCode::WrongTeam, "marking target is a teammate");
      if (!opponent->active) return Reject(CoachErrorCode::UnknownPlayer, "marking target is not active");
      instructions.marking_target = *target;
      return std::nullopt;
    }
    const auto* number = std::get_if<float>(&set->value);
    if (number == nullptr) return Reject(CoachErrorCode::MalformedCommand, "individual instruction expects a number");
    if (!std::isfinite(*number)) return Reject(CoachErrorCode::NonFiniteValue, "non-finite value");
    if (*number < 0.0f || *number > 1.0f) return Reject(CoachErrorCode::ValueOutOfRange, "value out of [0,1]");
    switch (set->key) {
      case IndividualKey::ForwardRuns: instructions.forward_runs = *number; break;
      case IndividualKey::Pressing: instructions.pressing = *number; break;
      case IndividualKey::PassingRisk: instructions.passing_risk = *number; break;
      case IndividualKey::Dribbling: instructions.dribbling = *number; break;
      case IndividualKey::PositionalFreedom: instructions.positional_freedom = *number; break;
      default: return Reject(CoachErrorCode::MalformedCommand, "individual key does not accept a number");
    }
    return std::nullopt;
  }

  if (const auto* set = std::get_if<SetTransitionInstruction>(&command)) {
    if (set->key == TransitionKey::OnWin) {
      const auto* value = std::get_if<TransitionOnWin>(&set->value);
      if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "on_win expects a mode");
      if (static_cast<std::size_t>(*value) >= 3) return Reject(CoachErrorCode::InvalidEnum, "invalid mode");
      plan.transition.on_win = *value;
    } else {
      const auto* value = std::get_if<TransitionOnLoss>(&set->value);
      if (value == nullptr) return Reject(CoachErrorCode::MalformedCommand, "on_loss expects a mode");
      if (static_cast<std::size_t>(*value) >= 3) return Reject(CoachErrorCode::InvalidEnum, "invalid mode");
      plan.transition.on_loss = *value;
    }
    return std::nullopt;
  }

  // Shouts are not a board edit and are rejected until the lifecycle lands.
  return Reject(CoachErrorCode::Unsupported, "shout lifecycle not enabled");
}
}  // namespace

football::sim::TeamSide CoachCommandSide(const CoachCommand& command) {
  return std::visit([](const auto& value) { return value.side; }, command);
}

CoachCommandResult CoachCommandExecutor::Apply(
    TacticalBoard& board, const CoachCommandEnvelope& envelope,
    const WorldState& world, std::uint64_t tick) const {
  if (envelope.observed_tick > tick)
    return Reject(CoachErrorCode::FutureTick, "observed tick is in the future");
  if (envelope.expires_at_tick != 0 && tick > envelope.expires_at_tick)
    return Reject(CoachErrorCode::Expired, "command expired");
  if (envelope.expected_board_revision != board.revision)
    return Reject(CoachErrorCode::StaleRevision, "board revision changed");
  if (world.phase == MatchPhase::Finished)
    return Reject(CoachErrorCode::InvalidPhase, "match is finished");

  // Transaction: validate and mutate a copy, commit only when fully valid.
  TacticalPlan candidate = board.plan;
  const std::optional<CoachCommandResult> failure =
      ApplyToPlan(candidate, envelope.command, world);
  if (failure) return *failure;
  if (!ValidateTacticalPlan(candidate).ok)
    return Reject(CoachErrorCode::MalformedCommand, "resulting plan is invalid");

  board.plan = candidate;
  board.revision += 1;
  CoachCommandResult result;
  result.accepted = true;
  result.error = CoachErrorCode::Ok;
  result.message = "ok";
  result.applied_tick = tick;
  result.new_board_revision = board.revision;
  return result;
}

}  // namespace football::ai

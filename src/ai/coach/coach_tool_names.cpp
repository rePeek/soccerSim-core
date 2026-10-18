#include "coach_tool_names.hpp"

namespace football::ai {
namespace {

template <typename E>
struct WireName {
  E value;
  std::string_view name;
};

template <typename E, std::size_t N>
std::string_view NameOf(E value, const WireName<E> (&table)[N]) {
  for (const WireName<E>& entry : table)
    if (entry.value == value) return entry.name;
  return {};
}

template <typename E, std::size_t N>
std::optional<E> Parse(std::string_view name, const WireName<E> (&table)[N]) {
  for (const WireName<E>& entry : table)
    if (entry.name == name) return entry.value;
  return std::nullopt;
}

constexpr WireName<InstructionKey> kInstructionKeys[] = {
    {InstructionKey::Mentality, "mentality"},
    {InstructionKey::AttackingWidth, "attacking_width"},
    {InstructionKey::Tempo, "tempo"},
    {InstructionKey::PassingDirectness, "passing_directness"},
    {InstructionKey::RiskTaking, "risk_taking"},
    {InstructionKey::CreativeFreedom, "creative_freedom"},
    {InstructionKey::AttackingBias, "attacking_bias"},
    {InstructionKey::BuildUp, "build_up"},
    {InstructionKey::PassIntoSpace, "pass_into_space"},
    {InstructionKey::DefensiveLine, "defensive_line"},
    {InstructionKey::LineOfEngagement, "line_of_engagement"},
    {InstructionKey::PressingIntensity, "pressing_intensity"},
    {InstructionKey::Compactness, "compactness"},
    {InstructionKey::Marking, "marking"},
    {InstructionKey::OffsideTrap, "offside_trap"},
};

constexpr WireName<IndividualKey> kIndividualKeys[] = {
    {IndividualKey::ForwardRuns, "forward_runs"},
    {IndividualKey::Pressing, "pressing"},
    {IndividualKey::PassingRisk, "passing_risk"},
    {IndividualKey::Dribbling, "dribbling"},
    {IndividualKey::PositionalFreedom, "positional_freedom"},
    {IndividualKey::MarkingTarget, "marking_target"},
};

constexpr WireName<TransitionKey> kTransitionKeys[] = {
    {TransitionKey::OnWin, "on_win"},
    {TransitionKey::OnLoss, "on_loss"},
};

constexpr WireName<Mentality> kMentalities[] = {
    {Mentality::VeryDefensive, "very_defensive"},
    {Mentality::Defensive, "defensive"},
    {Mentality::Cautious, "cautious"},
    {Mentality::Balanced, "balanced"},
    {Mentality::Positive, "positive"},
    {Mentality::Attacking, "attacking"},
    {Mentality::VeryAttacking, "very_attacking"},
};

constexpr WireName<BuildUpStyle> kBuildUpStyles[] = {
    {BuildUpStyle::Short, "short"},
    {BuildUpStyle::Balanced, "balanced"},
    {BuildUpStyle::Direct, "direct"},
};

constexpr WireName<MarkingStyle> kMarkingStyles[] = {
    {MarkingStyle::Zonal, "zonal"},
    {MarkingStyle::Mixed, "mixed"},
    {MarkingStyle::Man, "man"},
};

constexpr WireName<PossessionRole> kPossessionRoles[] = {
    {PossessionRole::Goalkeeper, "goalkeeper"},
    {PossessionRole::CentreBack, "centre_back"},
    {PossessionRole::FullBack, "full_back"},
    {PossessionRole::InvertedFullBack, "inverted_full_back"},
    {PossessionRole::HoldingMidfielder, "holding_midfielder"},
    {PossessionRole::Playmaker, "playmaker"},
    {PossessionRole::BoxToBox, "box_to_box"},
    {PossessionRole::Winger, "winger"},
    {PossessionRole::InsideForward, "inside_forward"},
    {PossessionRole::CentreForward, "centre_forward"},
    {PossessionRole::FalseNine, "false_nine"},
};

constexpr WireName<DefendingRole> kDefendingRoles[] = {
    {DefendingRole::Goalkeeper, "goalkeeper"},
    {DefendingRole::CoveringDefender, "covering_defender"},
    {DefendingRole::MarkingDefender, "marking_defender"},
    {DefendingRole::ScreeningMidfielder, "screening_midfielder"},
    {DefendingRole::PressingMidfielder, "pressing_midfielder"},
    {DefendingRole::TrackingWinger, "tracking_winger"},
    {DefendingRole::PressingForward, "pressing_forward"},
    {DefendingRole::HoldingForward, "holding_forward"},
};

constexpr WireName<TransitionOnWin> kTransitionOnWin[] = {
    {TransitionOnWin::Balanced, "balanced"},
    {TransitionOnWin::Counter, "counter"},
    {TransitionOnWin::HoldShape, "hold_shape"},
};

constexpr WireName<TransitionOnLoss> kTransitionOnLoss[] = {
    {TransitionOnLoss::Balanced, "balanced"},
    {TransitionOnLoss::CounterPress, "counter_press"},
    {TransitionOnLoss::Regroup, "regroup"},
};

constexpr WireName<TacticalPhase> kTacticalPhases[] = {
    {TacticalPhase::InPossession, "in_possession"},
    {TacticalPhase::OutOfPossession, "out_of_possession"},
};

constexpr WireName<ShoutType> kShoutTypes[] = {
    {ShoutType::Encourage, "encourage"},
    {ShoutType::Praise, "praise"},
    {ShoutType::DemandMore, "demand_more"},
    {ShoutType::FireUp, "fire_up"},
    {ShoutType::Berate, "berate"},
    {ShoutType::Focus, "focus"},
    {ShoutType::CalmDown, "calm_down"},
    {ShoutType::NoPressure, "no_pressure"},
};

}  // namespace

std::string_view ToWireName(InstructionKey key) {
  return NameOf(key, kInstructionKeys);
}
std::optional<InstructionKey> ParseInstructionKey(std::string_view name) {
  return Parse(name, kInstructionKeys);
}

InstructionValueKind KindOf(InstructionKey key) {
  switch (key) {
    case InstructionKey::Mentality: return InstructionValueKind::Mentality;
    case InstructionKey::BuildUp: return InstructionValueKind::BuildUpStyle;
    case InstructionKey::Marking: return InstructionValueKind::MarkingStyle;
    case InstructionKey::PassIntoSpace:
    case InstructionKey::OffsideTrap:
      return InstructionValueKind::Boolean;
    case InstructionKey::AttackingWidth:
    case InstructionKey::Tempo:
    case InstructionKey::PassingDirectness:
    case InstructionKey::RiskTaking:
    case InstructionKey::CreativeFreedom:
    case InstructionKey::AttackingBias:
    case InstructionKey::DefensiveLine:
    case InstructionKey::LineOfEngagement:
    case InstructionKey::PressingIntensity:
    case InstructionKey::Compactness:
      return InstructionValueKind::Number;
  }
  return InstructionValueKind::Number;
}

std::string_view ToWireName(IndividualKey key) {
  return NameOf(key, kIndividualKeys);
}
std::optional<IndividualKey> ParseIndividualKey(std::string_view name) {
  return Parse(name, kIndividualKeys);
}

IndividualValueKind KindOf(IndividualKey key) {
  return key == IndividualKey::MarkingTarget ? IndividualValueKind::MarkingTarget
                                             : IndividualValueKind::Number;
}

std::string_view ToWireName(TransitionKey key) {
  return NameOf(key, kTransitionKeys);
}
std::optional<TransitionKey> ParseTransitionKey(std::string_view name) {
  return Parse(name, kTransitionKeys);
}

std::string_view ToWireName(Mentality value) { return NameOf(value, kMentalities); }
std::optional<Mentality> ParseMentality(std::string_view name) {
  return Parse(name, kMentalities);
}

std::string_view ToWireName(BuildUpStyle value) { return NameOf(value, kBuildUpStyles); }
std::optional<BuildUpStyle> ParseBuildUpStyle(std::string_view name) {
  return Parse(name, kBuildUpStyles);
}

std::string_view ToWireName(MarkingStyle value) { return NameOf(value, kMarkingStyles); }
std::optional<MarkingStyle> ParseMarkingStyle(std::string_view name) {
  return Parse(name, kMarkingStyles);
}

std::string_view ToWireName(PossessionRole value) {
  return NameOf(value, kPossessionRoles);
}
std::optional<PossessionRole> ParsePossessionRole(std::string_view name) {
  return Parse(name, kPossessionRoles);
}

std::string_view ToWireName(DefendingRole value) {
  return NameOf(value, kDefendingRoles);
}
std::optional<DefendingRole> ParseDefendingRole(std::string_view name) {
  return Parse(name, kDefendingRoles);
}

std::string_view ToWireName(TransitionOnWin value) {
  return NameOf(value, kTransitionOnWin);
}
std::optional<TransitionOnWin> ParseTransitionOnWin(std::string_view name) {
  return Parse(name, kTransitionOnWin);
}

std::string_view ToWireName(TransitionOnLoss value) {
  return NameOf(value, kTransitionOnLoss);
}
std::optional<TransitionOnLoss> ParseTransitionOnLoss(std::string_view name) {
  return Parse(name, kTransitionOnLoss);
}

std::string_view ToWireName(TacticalPhase value) {
  return NameOf(value, kTacticalPhases);
}
std::optional<TacticalPhase> ParseTacticalPhase(std::string_view name) {
  return Parse(name, kTacticalPhases);
}

std::string_view ToWireName(ShoutType value) { return NameOf(value, kShoutTypes); }
std::optional<ShoutType> ParseShoutType(std::string_view name) {
  return Parse(name, kShoutTypes);
}

}  // namespace football::ai

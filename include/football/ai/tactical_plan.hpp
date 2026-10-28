#ifndef FOOTBALL_AI_TACTICAL_PLAN_HPP
#define FOOTBALL_AI_TACTICAL_PLAN_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "football/pitch/pitch.hpp"
#include "football/player/state/player_definition.hpp"
#include "football/runtime/match_config.hpp"
#include "football/team/lineup.hpp"

namespace football::ai {

// FM-style dual-phase tactical scheme.
//
// The plan is a pure, serialisable value model of the coach's *persistent*
// intent. It contains no live match state: no possession flag, no marking
// assignment, no player position, no score. Runtime interpretation lives in
// TacticalInterpreter (EffectiveTactics); runtime ownership of the plan stays
// with AIEngine.

// Formation slot identity, 0..10. Deliberately NOT a PlayerId: a slot keeps
// its meaning when the roster changes, and a squad binds players to slots
// outside the persistent plan.
using TacticalSlotId = football::team::FormationSlotId;
inline constexpr std::size_t kTacticalSlotCount = football::team::kFormationSlots;
inline constexpr auto kInvalidTacticalSlot = football::team::kInvalidFormationSlot;

enum class Mentality : std::uint8_t {
  VeryDefensive,
  Defensive,
  Cautious,
  Balanced,
  Positive,
  Attacking,
  VeryAttacking,
};

enum class PossessionRole : std::uint8_t {
  Goalkeeper,
  CentreBack,
  FullBack,
  InvertedFullBack,
  HoldingMidfielder,
  Playmaker,
  BoxToBox,
  Winger,
  InsideForward,
  CentreForward,
  FalseNine,
};

enum class DefendingRole : std::uint8_t {
  Goalkeeper,
  CoveringDefender,
  MarkingDefender,
  ScreeningMidfielder,
  PressingMidfielder,
  TrackingWinger,
  PressingForward,
  HoldingForward,
};

enum class TransitionOnWin : std::uint8_t { Balanced, Counter, HoldShape };
enum class TransitionOnLoss : std::uint8_t { Balanced, CounterPress, Regroup };

enum class BuildUpStyle : std::uint8_t { Short, Balanced, Direct };
enum class MarkingStyle : std::uint8_t { Zonal, Mixed, Man };

// Which phase a slot role/anchor edit targets. Coach commands carry it; the
// plan always stores both phases.
enum class TacticalPhase : std::uint8_t { InPossession, OutOfPossession };

// Anchor in canonical tactical coordinates, relative to this team's attacking
// direction:
//   u in [0,1]: 0 = own goal line, 1 = opponent goal line
//   v in [-1,1]: -1 = own right touchline, +1 = own left touchline (viewed
//                looking towards the opponent goal)
// No world metres and no fixed sign are stored here; the interpreter
// converts to the Stadium Frame with the actual defending_direction.
struct TacticalAnchor {
  float u = 0.5f;
  float v = 0.0f;
};

struct InPossessionInstructions {
  float attacking_width = 0.75f;    // [0,1]
  float tempo = 0.5f;               // [0,1]
  float passing_directness = 0.5f;  // [0,1]
  float risk_taking = 0.5f;         // [0,1]
  float creative_freedom = 0.5f;    // [0,1]
  float attacking_bias = 0.5f;      // [0,1]
  BuildUpStyle build_up = BuildUpStyle::Balanced;
  bool pass_into_space = false;
};

struct OutOfPossessionInstructions {
  float defensive_line = 0.55f;     // [0,1]
  float line_of_engagement = 0.5f;  // [0,1]
  float pressing_intensity = 0.5f;  // [0,1]
  float compactness = 0.5f;         // [0,1]
  MarkingStyle marking = MarkingStyle::Zonal;
  bool offside_trap = false;
};

struct TransitionInstructions {
  TransitionOnWin on_win = TransitionOnWin::Balanced;
  TransitionOnLoss on_loss = TransitionOnLoss::Balanced;
};

// Per-slot overrides. nullopt means "inherit the role / team default"; an
// explicit 0.0 is a real instruction and differs from "unset".
struct IndividualInstructions {
  std::optional<float> forward_runs;
  std::optional<float> pressing;
  std::optional<float> passing_risk;
  std::optional<float> dribbling;
  std::optional<float> positional_freedom;
  std::optional<football::player::PlayerId> marking_target;
};

struct TacticalSlot {
  TacticalSlotId id = 0;
  TacticalAnchor in_possession;
  TacticalAnchor out_of_possession;
  PossessionRole in_possession_role = PossessionRole::CentreBack;
  DefendingRole out_of_possession_role = DefendingRole::CoveringDefender;
  IndividualInstructions instructions;
};

struct TacticalPlan {
  Mentality mentality = Mentality::Balanced;
  InPossessionInstructions in_possession;
  OutOfPossessionInstructions out_of_possession;
  TransitionInstructions transition;
  std::array<TacticalSlot, kTacticalSlotCount> slots{};
};

// Slot -> player binding view. Owned by the match squad / coach runtime, never
// by the persistent plan. Empty slots are legal (red card, fewer players).
struct SlotAssignment {
  std::array<std::optional<football::player::PlayerId>, kTacticalSlotCount> players{};
};

enum class TacticalErrorCode : std::uint8_t {
  Ok = 0,
  InvalidSlotId,
  DuplicateSlotId,
  NonFiniteValue,
  ValueOutOfRange,
  InvalidEnum,
  InvalidMarkingTarget,
  InvalidRevision,
};

struct TacticalValidation {
  bool ok = true;
  TacticalErrorCode error = TacticalErrorCode::Ok;
  std::size_t slot_index = 0;  // meaningful when the error is slot-scoped
  const char* message = "";

  explicit operator bool() const { return ok; }
};

namespace detail {
template <typename E>
constexpr bool ValidEnum(E value, std::size_t count) {
  return static_cast<std::size_t>(value) < count;
}
constexpr bool FiniteInRange(float value, float low, float high) {
  // NaN fails both comparisons; this rejects non-finite and out-of-range.
  return value >= low && value <= high;
}
constexpr bool OptionalInRange(const std::optional<float>& value) {
  return !value.has_value() || FiniteInRange(*value, 0.0f, 1.0f);
}
}  // namespace detail

// Pure structural validation. Same input -> same result, no allocation.
inline TacticalValidation ValidateTacticalPlan(const TacticalPlan& plan) {
  using football::ai::detail::FiniteInRange;
  using football::ai::detail::OptionalInRange;
  using football::ai::detail::ValidEnum;

  const auto fail = [](TacticalErrorCode code, std::size_t slot,
                       const char* message) {
    TacticalValidation result;
    result.ok = false;
    result.error = code;
    result.slot_index = slot;
    result.message = message;
    return result;
  };

  if (!ValidEnum(plan.mentality, 7))
    return fail(TacticalErrorCode::InvalidEnum, 0, "invalid mentality");

  const InPossessionInstructions& ip = plan.in_possession;
  if (!FiniteInRange(ip.attacking_width, 0.0f, 1.0f) ||
      !FiniteInRange(ip.tempo, 0.0f, 1.0f) ||
      !FiniteInRange(ip.passing_directness, 0.0f, 1.0f) ||
      !FiniteInRange(ip.risk_taking, 0.0f, 1.0f) ||
      !FiniteInRange(ip.creative_freedom, 0.0f, 1.0f) ||
      !FiniteInRange(ip.attacking_bias, 0.0f, 1.0f))
    return fail(TacticalErrorCode::ValueOutOfRange, 0,
                "in-possession value out of [0,1] or non-finite");
  if (!ValidEnum(ip.build_up, 3))
    return fail(TacticalErrorCode::InvalidEnum, 0, "invalid build-up style");

  const OutOfPossessionInstructions& oop = plan.out_of_possession;
  if (!FiniteInRange(oop.defensive_line, 0.0f, 1.0f) ||
      !FiniteInRange(oop.line_of_engagement, 0.0f, 1.0f) ||
      !FiniteInRange(oop.pressing_intensity, 0.0f, 1.0f) ||
      !FiniteInRange(oop.compactness, 0.0f, 1.0f))
    return fail(TacticalErrorCode::ValueOutOfRange, 0,
                "out-of-possession value out of [0,1] or non-finite");
  if (!ValidEnum(oop.marking, 3))
    return fail(TacticalErrorCode::InvalidEnum, 0, "invalid marking style");

  if (!ValidEnum(plan.transition.on_win, 3) ||
      !ValidEnum(plan.transition.on_loss, 3))
    return fail(TacticalErrorCode::InvalidEnum, 0, "invalid transition mode");

  std::array<bool, kTacticalSlotCount> seen{};
  for (std::size_t i = 0; i < plan.slots.size(); ++i) {
    const TacticalSlot& slot = plan.slots[i];
    if (slot.id >= kTacticalSlotCount)
      return fail(TacticalErrorCode::InvalidSlotId, i, "slot id out of range");
    if (seen[slot.id])
      return fail(TacticalErrorCode::DuplicateSlotId, i, "duplicate slot id");
    seen[slot.id] = true;

    if (!FiniteInRange(slot.in_possession.u, 0.0f, 1.0f) ||
        !FiniteInRange(slot.in_possession.v, -1.0f, 1.0f) ||
        !FiniteInRange(slot.out_of_possession.u, 0.0f, 1.0f) ||
        !FiniteInRange(slot.out_of_possession.v, -1.0f, 1.0f))
      return fail(TacticalErrorCode::ValueOutOfRange, i, "anchor out of range");

    if (!ValidEnum(slot.in_possession_role, 11) ||
        !ValidEnum(slot.out_of_possession_role, 8))
      return fail(TacticalErrorCode::InvalidEnum, i, "invalid slot role");

    const IndividualInstructions& individual = slot.instructions;
    if (!OptionalInRange(individual.forward_runs) ||
        !OptionalInRange(individual.pressing) ||
        !OptionalInRange(individual.passing_risk) ||
        !OptionalInRange(individual.dribbling) ||
        !OptionalInRange(individual.positional_freedom))
      return fail(TacticalErrorCode::ValueOutOfRange, i,
                  "individual instruction out of [0,1] or non-finite");
    if (individual.marking_target &&
        *individual.marking_target == football::player::kInvalidPlayerId)
      return fail(TacticalErrorCode::InvalidMarkingTarget, i,
                  "marking target is not a valid PlayerId");
  }
  return TacticalValidation{};
}

// Team-instruction defaults implied by a mentality. Choosing a tactic fills the
// concrete numeric fields once; later coach edits change the field itself and
// are never silently re-multiplied by the mentality.
inline InPossessionInstructions DefaultInPossession(Mentality mentality) {
  InPossessionInstructions ip;
  switch (mentality) {
    case Mentality::VeryDefensive:
      ip.tempo = 0.35f; ip.passing_directness = 0.35f; ip.risk_taking = 0.2f;
      ip.attacking_bias = 0.2f; ip.attacking_width = 0.6f; break;
    case Mentality::Defensive:
      ip.tempo = 0.4f; ip.passing_directness = 0.4f; ip.risk_taking = 0.3f;
      ip.attacking_bias = 0.35f; ip.attacking_width = 0.68f; break;
    case Mentality::Cautious:
      ip.tempo = 0.45f; ip.passing_directness = 0.45f; ip.risk_taking = 0.4f;
      ip.attacking_bias = 0.43f; ip.attacking_width = 0.72f; break;
    case Mentality::Balanced:
      break;
    case Mentality::Positive:
      ip.tempo = 0.55f; ip.passing_directness = 0.55f; ip.risk_taking = 0.6f;
      ip.attacking_bias = 0.58f; ip.attacking_width = 0.78f; break;
    case Mentality::Attacking:
      ip.tempo = 0.65f; ip.passing_directness = 0.62f; ip.risk_taking = 0.7f;
      ip.attacking_bias = 0.7f; ip.attacking_width = 0.82f; break;
    case Mentality::VeryAttacking:
      ip.tempo = 0.75f; ip.passing_directness = 0.7f; ip.risk_taking = 0.8f;
      ip.attacking_bias = 0.82f; ip.attacking_width = 0.88f; break;
  }
  return ip;
}

inline OutOfPossessionInstructions DefaultOutOfPossession(Mentality mentality) {
  OutOfPossessionInstructions oop;
  switch (mentality) {
    case Mentality::VeryDefensive:
      oop.defensive_line = 0.25f; oop.line_of_engagement = 0.25f;
      oop.pressing_intensity = 0.25f; oop.compactness = 0.7f; break;
    case Mentality::Defensive:
      oop.defensive_line = 0.35f; oop.line_of_engagement = 0.35f;
      oop.pressing_intensity = 0.35f; oop.compactness = 0.65f; break;
    case Mentality::Cautious:
      oop.defensive_line = 0.45f; oop.line_of_engagement = 0.43f;
      oop.pressing_intensity = 0.42f; oop.compactness = 0.6f; break;
    case Mentality::Balanced:
      break;
    case Mentality::Positive:
      oop.defensive_line = 0.6f; oop.line_of_engagement = 0.58f;
      oop.pressing_intensity = 0.6f; oop.compactness = 0.48f; break;
    case Mentality::Attacking:
      oop.defensive_line = 0.68f; oop.line_of_engagement = 0.68f;
      oop.pressing_intensity = 0.7f; oop.compactness = 0.42f; break;
    case Mentality::VeryAttacking:
      oop.defensive_line = 0.75f; oop.line_of_engagement = 0.78f;
      oop.pressing_intensity = 0.8f; oop.compactness = 0.38f; break;
  }
  return oop;
}

// Role anchors in canonical tactical coordinates. Used when a team declares no
// explicit formation position for a slot.
TacticalAnchor PossessionRoleAnchor(PossessionRole role);
TacticalAnchor DefendingRoleAnchor(DefendingRole role);

// Deterministically build the plan from the declared static team shape. The
// two phases start identical; later phases may diverge through explicit coach
// intent. Slot ids are stamped 0..10 in declared order.
TacticalPlan MakeTacticalPlan(const football::sim::TeamSetup& team,
                                  football::sim::TeamSide side,
                                  const football::pitch::Pitch& pitch);

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_PLAN_HPP

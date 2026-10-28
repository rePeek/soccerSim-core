#ifndef FOOTBALL_AI_TACTICAL_INTERPRETER_HPP
#define FOOTBALL_AI_TACTICAL_INTERPRETER_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "football/ai/tactical_board.hpp"
#include "football/ai/tactical_plan.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai {

// EffectiveTactics is the *interpretation* of a persistent plan, not a second
// board. It is produced fresh every call from (plan, world, slot binding, phase
// state) and is never written back. Player AI may keep its own dynamic plans
// (marking, runs, candidates); those never live here.

// Canonical tactical coordinates <-> observed world metres. `defending_direction`
// is the value observed in WorldState, never an assumed per-half constant.
football::math::Vector3 TacticalAnchorToWorld(const TacticalAnchor& anchor,
                                              int defending_direction,
                                              const football::pitch::Pitch& pitch);
TacticalAnchor WorldPositionToAnchor(const football::math::Vector3& position,
                                     int defending_direction,
                                     const football::pitch::Pitch& pitch);

// The active phase is an input, not stored state: possession is a match fact.
TacticalPhase PhaseFromPossession(bool team_has_possession);

// Uses Runtime's stable slot binding when present, filtering unavailable actors.
// Synthetic observations without metadata retain active roster-order fallback.
// An authoritative vacancy must never compress the remaining slots.
SlotAssignment DefaultSlotAssignment(const WorldState& world,
                                     football::sim::TeamSide side);

// Player-AI-supplied continuity state. The interpreter is a pure function of
// this plus the board and the world.
struct InterpretationState {
  std::array<TacticalPhase, 2> phase{TacticalPhase::OutOfPossession,
                                     TacticalPhase::OutOfPossession};
  // -1 .. +1 momentum for each side: +1 just won the ball, -1 just lost it.
  // Zero is steady open play. Used only to bias transitions, not to store state.
  std::array<float, 2> transition_momentum{0.0f, 0.0f};

  static InterpretationState FromWorld(const WorldState& world);
};

struct EffectivePlayerTactics {
  TacticalSlotId slot = 0;
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  bool assigned = false;
  // True when the plan's slot is the goalkeeper, so a consumer never has to
  // guess the role from the observation.
  bool goalkeeper = false;
  football::math::Vector3 target_anchor_metres = football::math::Vector3(0);
  float forward_run_bias = 0.5f;
  float pass_risk = 0.5f;
  float press_bias = 0.5f;
  float dribble_bias = 0.5f;
  float positional_freedom = 0.5f;
  // Exclusive man-mark target when the plan asks for one; conflicts are flagged
  // on the team result, never silently accepted.
  std::optional<football::player::PlayerId> mark_target;
};

struct EffectiveTeamTactics {
  TacticalPhase phase = TacticalPhase::OutOfPossession;
  float tempo = 0.5f;
  float risk = 0.5f;
  float press = 0.5f;
  float defensive_line = 0.55f;
  float engagement_line = 0.5f;
  float width = 0.75f;
  float compactness = 0.5f;
  float transition_momentum = 0.0f;
  TransitionOnWin on_win = TransitionOnWin::Balanced;
  TransitionOnLoss on_loss = TransitionOnLoss::Balanced;
  MarkingStyle marking = MarkingStyle::Zonal;
  bool offside_trap = false;
  // True when two occupied slots request the same man-mark target.
  bool marking_conflict = false;
};

struct TacticalInterpretation {
  std::array<EffectiveTeamTactics, 2> teams{};
  // Per-side effective tactics indexed by slot index 0..10.
  std::array<std::array<EffectivePlayerTactics, kTacticalSlotCount>, 2> players{};
};

// Pure interpreter. Every board holds a formal plan, so a side always has an
// interpreted plan; there is no legacy fallback path.
class TacticalInterpreter {
 public:
  static TacticalInterpretation Build(
      std::span<const TacticalBoard> boards, const WorldState& world,
      const std::array<SlotAssignment, 2>& assignments,
      const InterpretationState& state);
};

// The assigned effective tactics for a player, or nullptr when the plan has no
// occupied slot bound to that identity.
inline const EffectivePlayerTactics* FindEffective(
    const std::array<EffectivePlayerTactics, kTacticalSlotCount>& players,
    football::player::PlayerId player) {
  for (const EffectivePlayerTactics& candidate : players)
    if (candidate.assigned && candidate.player == player) return &candidate;
  return nullptr;
}

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_INTERPRETER_HPP

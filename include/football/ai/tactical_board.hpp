#ifndef FOOTBALL_AI_TACTICAL_BOARD_HPP
#define FOOTBALL_AI_TACTICAL_BOARD_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"
#include "football/pitch/pitch.hpp"
#include "football/ai/tactical_plan.hpp"

namespace football::ai {

// Planned roles are AI intent, separate from runtime formation/animation enums.
enum class PlannedPlayerRole {
  Unspecified,
  Goalkeeper,
  Defender,
  Midfielder,
  Forward,
};

struct PlayerDirective {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  PlannedPlayerRole role = PlannedPlayerRole::Unspecified;
  // Base anchor in the same Stadium Frame/metres as WorldState. Player AI
  // adapts a local copy; it never writes a tick's target back into this intent.
  std::optional<football::math::Vector3> formation_position;
  std::optional<football::player::PlayerId> marking_target;
};

// Explicit board version. Legacy and V2 are never inferred implicitly from a
// bool or a default value; every reader switches on the mode.
enum class BoardMode : std::uint8_t { Legacy, V2 };

// Persistent AI-owned configuration. External coaches/UI may edit these values.
// Actual restarts belong to sim/WorldState; policy owns no transient channel.
//
// Exactly one authority per side: either the legacy fields (Legacy) or the
// advanced plan (V2), never both. `revision` is a monotonic counter bumped by
// the coach command executor; it is the concurrency token for stale writes.
struct TacticalBoard {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  BoardMode mode = BoardMode::Legacy;
  // Legacy fields. Read only while mode == Legacy; ignored in V2.
  // Desired dimensions as fractions of pitch width/length, not metres.
  float width = 0.75f;
  float depth = 0.55f;
  std::vector<PlayerDirective> players;
  // V2 plan. Present if and only if mode == V2.
  std::optional<TacticalPlanV2> advanced;
  // Monotonic revision, bumped on every accepted coach write.
  std::uint64_t revision = 0;
};

// Invariant check for the mode/advanced pairing. Pure, no allocation.
inline bool ValidateTacticalBoard(const TacticalBoard &board) {
  const bool has_plan = board.advanced.has_value();
  if ((board.mode == BoardMode::V2) != has_plan) return false;
  if (board.mode == BoardMode::Legacy) return true;
  return ValidateTacticalPlan(*board.advanced).ok;
}

// The advanced plan, or nullptr in Legacy mode. The only supported way to
// read v2 intent.
inline const TacticalPlanV2 *AdvancedPlan(const TacticalBoard &board) {
  return board.mode == BoardMode::V2 ? &*board.advanced : nullptr;
}
inline TacticalPlanV2 *AdvancedPlan(TacticalBoard &board) {
  return board.mode == BoardMode::V2 ? &*board.advanced : nullptr;
}

// Bootstrap desired shape from static declarations, without observing sim.
TacticalBoard MakeTacticalBoard(const football::sim::TeamSetup &team, football::sim::TeamSide side,
                               const football::pitch::Pitch &pitch);

// Bootstrap a V2 board from the same static declarations. Legacy fields are
// left at their defaults so no accidental legacy read is possible.
TacticalBoard MakeTacticalBoardV2(const football::sim::TeamSetup &team,
                                 football::sim::TeamSide side,
                                 const football::pitch::Pitch &pitch);

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_BOARD_HPP

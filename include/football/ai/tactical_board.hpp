#ifndef FOOTBALL_AI_TACTICAL_BOARD_HPP
#define FOOTBALL_AI_TACTICAL_BOARD_HPP

#include <optional>
#include <vector>

#include "sim/player/player_definition.hpp"
#include "sim/runtime/match_config.hpp"
#include "football/pitch/pitch.hpp"
#include "football/foundation/math/vector3.hpp"

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
  // Base anchor in the same home pitch frame/metres as WorldState. Player AI
  // adapts a local copy; it never writes a tick's target back into this intent.
  std::optional<blunted::Vector3> formation_position;
  std::optional<football::player::PlayerId> marking_target;
};

// Persistent AI-owned configuration. External coaches/UI may edit these values.
// Actual restarts belong to sim/WorldState; policy owns no transient channel.
struct TacticalBoard {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  // Desired dimensions as fractions of pitch width/length, not metres.
  float width = 0.75f;
  float depth = 0.55f;
  std::vector<PlayerDirective> players;
};

// Bootstrap desired shape from static declarations, without observing sim.
TacticalBoard MakeTacticalBoard(const football::sim::TeamSetup &team, football::sim::TeamSide side,
                               const football::pitch::Pitch &pitch);

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_BOARD_HPP

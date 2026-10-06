#ifndef FOOTBALL_AI_TACTICAL_BOARD_HPP
#define FOOTBALL_AI_TACTICAL_BOARD_HPP

#include <optional>
#include <vector>

#include "model/player.hpp"
#include "model/team.hpp"
#include "foundation/math/vector3.hpp"

// Planned roles are AI intent, separate from runtime formation/animation enums.
enum class PlannedPlayerRole {
  Unspecified,
  Goalkeeper,
  Defender,
  Midfielder,
  Forward,
};

struct PlayerDirective {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  PlannedPlayerRole role = PlannedPlayerRole::Unspecified;
  // Base anchor in the same home pitch frame/metres as WorldState. Player AI
  // adapts a local copy; it never writes a tick's target back into this intent.
  std::optional<blunted::Vector3> formation_position;
  std::optional<football::model::PlayerId> marking_target;
};

// Persistent AI-owned configuration. External coaches/UI may edit these values.
// Actual restarts and timed run/pressure/rush requests belong to WorldState.
struct TacticalBoard {
  football::model::TeamSide side = football::model::TeamSide::Home;
  // Desired shape dimensions in metres; zero selects the policy's pitch-based
  // default (75% width / 55% length). Reading does not resolve/write defaults.
  float width = 0.0f;
  float depth = 0.0f;
  std::vector<PlayerDirective> players;
};

#endif  // FOOTBALL_AI_TACTICAL_BOARD_HPP

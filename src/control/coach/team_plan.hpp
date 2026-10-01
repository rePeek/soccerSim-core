#ifndef FOOTBALL_CONTROL_COACH_TEAM_PLAN_HPP
#define FOOTBALL_CONTROL_COACH_TEAM_PLAN_HPP

#include <optional>
#include <vector>

#include "control/control_ids.hpp"
#include "foundation/math/vector3.hpp"

// Coarse role assignment owned by the decision domain. It is intentionally
// separate from simulation's current formation and animation enums.
enum class PlannedPlayerRole {
  Unspecified,
  Goalkeeper,
  Defender,
  Midfielder,
  Forward,
};

struct PlayerDirective {
  PlayerId player = kInvalidPlayerId;
  PlannedPlayerRole role = PlannedPlayerRole::Unspecified;
  std::optional<blunted::Vector3> formation_position;
  std::optional<PlayerId> marking_target;
  bool attacking_run = false;
  bool press = false;
};

struct TeamPlan {
  TeamId team = kInvalidTeamId;
  float width = 0.0f;
  float depth = 0.0f;
  std::vector<PlayerDirective> players;
  std::optional<PlayerId> set_piece_taker;
};

#endif  // FOOTBALL_CONTROL_COACH_TEAM_PLAN_HPP

#ifndef FOOTBALL_CONTROL_TACTICAL_BOARD_HPP
#define FOOTBALL_CONTROL_TACTICAL_BOARD_HPP

#include <optional>
#include <vector>

#include "domain/ids.hpp"
#include "foundation/math/vector3.hpp"

// Coarse role assignment owned by persistent team-control state. It is
// deliberately separate from simulation formation and animation enums.
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

// Persistent team-control state. Coach AI mutates this board; player AI reads
// it when producing frame-local PlayerControl values.
struct TacticalBoard {
  TeamId team = kInvalidTeamId;
  float width = 0.0f;
  float depth = 0.0f;
  std::vector<PlayerDirective> players;
  std::optional<PlayerId> set_piece_taker;
};

#endif  // FOOTBALL_CONTROL_TACTICAL_BOARD_HPP

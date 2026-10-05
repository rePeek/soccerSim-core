#ifndef FOOTBALL_STATE_WORLD_STATE_HPP
#define FOOTBALL_STATE_WORLD_STATE_HPP

#include <cstdint>
#include <vector>

#include "model/ids.hpp"
#include "foundation/math/vector3.hpp"

struct WorldPlayerState {
  football::model::PlayerId id = football::model::kInvalidPlayerId;
  football::model::TeamId team = football::model::kInvalidTeamId;
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(1, 0, 0);
  bool active = false;
  bool has_possession = false;
};

// Immutable-by-convention value snapshot of authoritative match state. It can
// be retained for replay, training data, and diagnostics without exposing
// mutable simulation objects.
struct WorldState {
  std::uint64_t tick = 0;
  blunted::Vector3 ball_position = blunted::Vector3(0);
  std::vector<WorldPlayerState> players;
};

#endif  // FOOTBALL_STATE_WORLD_STATE_HPP

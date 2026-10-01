#ifndef FOOTBALL_CONTROL_WORLD_STATE_VIEW_HPP
#define FOOTBALL_CONTROL_WORLD_STATE_VIEW_HPP

#include <cstdint>
#include <span>

#include "control/control_ids.hpp"
#include "foundation/math/vector3.hpp"

struct WorldPlayerState {
  PlayerId id = kInvalidPlayerId;
  TeamId team = kInvalidTeamId;
  blunted::Vector3 position = blunted::Vector3(0);
  blunted::Vector3 velocity = blunted::Vector3(0);
  blunted::Vector3 facing = blunted::Vector3(1, 0, 0);
  bool active = false;
  bool has_possession = false;
};

// Read-only projection of authoritative match state for control decisions.
// Simulation will provide its concrete view; controls never receive mutable
// Match, Team, or Player objects.
class WorldStateView {
 public:
  virtual ~WorldStateView() = default;

  virtual std::uint64_t tick() const = 0;
  virtual blunted::Vector3 ball_position() const = 0;
  virtual std::span<const WorldPlayerState> players() const = 0;
};

#endif  // FOOTBALL_CONTROL_WORLD_STATE_VIEW_HPP

#ifndef FOOTBALL_CONTROL_PLAYER_CONTROL_HPP
#define FOOTBALL_CONTROL_PLAYER_CONTROL_HPP

#include <optional>

#include "domain/ids.hpp"
#include "foundation/math/vector3.hpp"

// Football-domain action request. This intentionally does not expose input
// buttons, animation states, or simulation execution enums.
enum class ControlAction {
  None,
  ShortPass,
  LongPass,
  HighPass,
  Shoot,
  Tackle,
};

// Frame-local control state for one player. Player AI writes these values;
// simulation translates them into its existing command queue.
struct PlayerControl {
  PlayerId player = kInvalidPlayerId;
  blunted::Vector3 move_direction = blunted::Vector3(0);
  float desired_speed = 0.0f;
  std::optional<blunted::Vector3> look_at;
  ControlAction action = ControlAction::None;
  std::optional<PlayerId> target_player;
  std::optional<blunted::Vector3> target_position;
  float power = 0.0f;
};

#endif  // FOOTBALL_CONTROL_PLAYER_CONTROL_HPP

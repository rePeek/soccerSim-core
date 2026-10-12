#ifndef FOOTBALL_SIM_PLAYER_CONTROL_HPP
#define FOOTBALL_SIM_PLAYER_CONTROL_HPP

#include <optional>

#include "sim/player/player_definition.hpp"
#include "football/foundation/math/vector3.hpp"

// Football-domain action request. This intentionally does not expose input
// buttons, animation states, or simulation execution enums.
enum class ControlAction {
  None,
  ShortPass,
  LongPass,
  HighPass,
  Shoot,
  Tackle,
  Dribble,
  Trap,
  Save,
};

// Executable request for one simulation tick, independent of its input source.
// A caller may retain/resubmit it; simulation translates it into commands.
struct PlayerControl {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  blunted::Vector3 move_direction = blunted::Vector3(0);
  float desired_speed = 0.0f;
  std::optional<blunted::Vector3> look_at;
  ControlAction action = ControlAction::None;
  std::optional<football::player::PlayerId> target_player;
  std::optional<blunted::Vector3> target_position;
  float power = 0.0f;
};

#endif  // FOOTBALL_SIM_PLAYER_CONTROL_HPP

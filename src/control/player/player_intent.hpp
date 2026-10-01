#ifndef FOOTBALL_CONTROL_PLAYER_PLAYER_INTENT_HPP
#define FOOTBALL_CONTROL_PLAYER_PLAYER_INTENT_HPP

#include <optional>

#include "control/control_ids.hpp"
#include "foundation/math/vector3.hpp"

// Decision-domain request. It deliberately does not expose input devices,
// animation frames, or simulation execution state.
enum class IntentAction {
  None,
  ShortPass,
  LongPass,
  HighPass,
  Shoot,
  Tackle,
};

struct PlayerIntent {
  blunted::Vector3 move_direction = blunted::Vector3(0);
  float desired_speed = 0.0f;
  std::optional<blunted::Vector3> look_at;
  IntentAction action = IntentAction::None;
  std::optional<PlayerId> target_player;
  std::optional<blunted::Vector3> target_position;
  float power = 0.0f;
};

#endif  // FOOTBALL_CONTROL_PLAYER_PLAYER_INTENT_HPP

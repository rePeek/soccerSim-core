#ifndef FOOTBALL_PLAYER_COMMAND_ACTION_REQUEST_HPP
#define FOOTBALL_PLAYER_COMMAND_ACTION_REQUEST_HPP

#include <cstdint>
#include "football/player/player_control.hpp"

// One-shot active input. Continuous movement belongs to SetControl; both use
// the same ControlAction vocabulary. Acceptance is not execution or contact.
struct PlayerActionRequest {
  ControlAction action = ControlAction::None;
  // Direction and absolute target are in the Stadium Frame.
  football::math::Vector3 direction = football::math::Vector3(0);
  float speed = 0.0f;
  football::math::Vector3 target_position = football::math::Vector3(0);
  bool has_target_position = false;
  football::player::PlayerId target_player = football::player::kInvalidPlayerId;
  float power = 0.0f;
};

enum class ReactionKind : std::uint8_t {
  None = 0,
  Trip,
  Bump,
};

// Contact supplies the physical fact; Player Execution decides how its current
// action reacts. No animation clip or playback state crosses this boundary.
struct PlayerReaction {
  ReactionKind kind = ReactionKind::None;
  football::math::Vector3 direction = football::math::Vector3(0);
  float severity = 0.0f;
  football::player::PlayerId source = football::player::kInvalidPlayerId;
  int trip_type = 0;
};

enum class RequestOutcome : std::uint8_t {
  Accepted = 0,
  Deferred,
  Rejected,
  Unsupported,
};

#endif

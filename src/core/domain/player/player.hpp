#ifndef _HPP_CORE_DOMAIN_PLAYER
#define _HPP_CORE_DOMAIN_PLAYER

#include "core/state/player_state.hpp"

namespace football::domain {

// Player is the football domain entity (Phase 7F-0).
//
// It references the authoritative PlayerState and exposes read access to the
// player's movement. Deliberately thin: no animation, AI, possession, tackle
// or stats — those stay in the legacy Player/PlayerBase facade and later
// migrate to Intent/Action.
class Player {
public:
  explicit Player(PlayerState& state);

  PlayerState& State() { return state_; }
  const PlayerState& State() const { return state_; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  const blunted::Vector3& Facing() const { return state_.facing; }

private:
  PlayerState& state_;
};

}  // namespace football::domain

#endif  // _HPP_CORE_DOMAIN_PLAYER
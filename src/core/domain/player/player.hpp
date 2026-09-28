#ifndef _HPP_CORE_DOMAIN_PLAYER
#define _HPP_CORE_DOMAIN_PLAYER

#include "core/state/player_state.hpp"
#include "player_profile.hpp"

namespace football::domain {

// Player is the football domain entity (Phase 7F-0).
//
// It binds a match-lifetime profile to the World-owned state without owning
// movement algorithms. Animation, AI and action compatibility stay in the
// legacy Player/PlayerBase facade.
class Player {
public:
  Player(const PlayerProfile& profile, PlayerState& state);
  const PlayerProfile& Profile() const { return profile_; }

  PlayerState& State() { return state_; }
  const PlayerState& State() const { return state_; }

  const blunted::Vector3& Position() const { return state_.position; }
  const blunted::Vector3& Velocity() const { return state_.velocity; }
  const blunted::Vector3& MovementFacing() const { return state_.movementFacing; }
  const blunted::Vector3& TorsoFacing() const { return state_.torsoFacing; }

private:
  const PlayerProfile& profile_;
  PlayerState& state_;
};

}  // namespace football::domain

#endif  // _HPP_CORE_DOMAIN_PLAYER
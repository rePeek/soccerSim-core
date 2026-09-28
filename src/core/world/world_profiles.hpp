#ifndef _HPP_CORE_WORLD_WORLD_PROFILES
#define _HPP_CORE_WORLD_WORLD_PROFILES

#include <array>
#include "defines.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/domain/player/player_profile.hpp"

// Match-lifetime configuration; not copied with WorldState predictions or
// serialized as part of the dynamic simulation snapshot.
struct WorldProfiles {
  football::domain::BallProfile ball;
  std::array<football::domain::PlayerProfile, 2 * MAX_PLAYERS> players;
};

#endif  // _HPP_CORE_WORLD_WORLD_PROFILES

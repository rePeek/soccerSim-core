#ifndef _HPP_CORE_WORLD_WORLD_PROFILES
#define _HPP_CORE_WORLD_WORLD_PROFILES

#include <array>
#include "defines.hpp"
#include "core/model/ball/ball_profile.hpp"
#include "core/model/player/player_profile.hpp"

// Match-lifetime configuration; not copied with WorldState predictions or
// serialized as part of the dynamic simulation snapshot.
struct WorldProfiles {
  football::model::BallProfile ball;
  std::array<football::model::PlayerProfile, 2 * MAX_PLAYERS> players;
};

#endif  // _HPP_CORE_WORLD_WORLD_PROFILES

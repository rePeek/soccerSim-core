#ifndef _HPP_CORE_WORLD_WORLD_PROFILES
#define _HPP_CORE_WORLD_WORLD_PROFILES

#include "core/domain/ball/ball_profile.hpp"

// Match-lifetime configuration; not copied with WorldState predictions or
// serialized as part of the dynamic simulation snapshot.
struct WorldProfiles {
  football::domain::BallProfile ball;
};

#endif  // _HPP_CORE_WORLD_WORLD_PROFILES

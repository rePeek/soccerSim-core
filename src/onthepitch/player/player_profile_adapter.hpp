#ifndef _HPP_PLAYER_PROFILE_ADAPTER
#define _HPP_PLAYER_PROFILE_ADAPTER

#include "core/domain/player/player_profile.hpp"
#include "data/playerdata.hpp"

// Boundary from database/legacy data to match-lifetime simulation properties.
// Do not fabricate mass or strength from unrelated legacy balance/height stats.
inline football::domain::PlayerProfile MakeSimulationPlayerProfile(
    const PlayerData& data) {
  football::domain::PlayerProfile profile;
  profile.physical.height = data.GetHeight();
  profile.physical.balance = data.GetStat(physical_balance);
  return profile;
}

#endif  // _HPP_PLAYER_PROFILE_ADAPTER

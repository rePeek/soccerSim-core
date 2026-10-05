#ifndef FOOTBALL_DATA_PLAYER_PROFILE_HPP
#define FOOTBALL_DATA_PLAYER_PROFILE_HPP

#include "model/player.hpp"

namespace football::data {

// Resolves legacy names, appearance and age-adjusted abilities into a static
// model. No GameContext or simulation RNG is needed; unknown keys retain the
// legacy neutral defaults and unspecified skin colour.
model::Player LoadLegacyPlayerProfile(model::PlayerDatabaseId database_id,
                                     bool left_team);

}  // namespace football::data

#endif  // FOOTBALL_DATA_PLAYER_PROFILE_HPP

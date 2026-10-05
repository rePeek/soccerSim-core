#ifndef FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP
#define FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP

#include "model/player.hpp"

namespace football::app::fixtures {

// App-side legacy import, not simulation startup. Unknown keys retain neutral
// defaults and unspecified skin colour; provenance never supplies identity.
model::Player LoadLegacyPlayerProfile(model::PlayerDatabaseId database_id,
                                     bool left_team);

}  // namespace football::app::fixtures

#endif  // FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP

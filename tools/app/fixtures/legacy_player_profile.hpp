#ifndef FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP
#define FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP

#include "sim/player/player_definition.hpp"

namespace football::app::fixtures {

// Typed sample fixture retaining the historical profile values and quantization.
// Unknown keys retain neutral defaults; provenance never supplies identity.
football::player::PlayerDefinition LoadLegacyPlayerProfile(football::player::PlayerDatabaseId database_id,
                                     bool left_team);

}  // namespace football::app::fixtures

#endif  // FOOTBALL_APP_FIXTURES_LEGACY_PLAYER_PROFILE_HPP

#ifndef FOOTBALL_MODEL_PLAYER_HPP
#define FOOTBALL_MODEL_PLAYER_HPP

#include "model/ids.hpp"

namespace football::model {

// Static roster identity. The legacy profile adapter still resolves names and
// attributes from this key when constructing a match. Describing a player here
// does not allocate a runtime player or consume simulation randomness.
struct Player {
  PlayerDatabaseId database_id = 0;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_PLAYER_HPP

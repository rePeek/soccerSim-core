#ifndef FOOTBALL_ENV_MODEL_ADAPTER_HPP
#define FOOTBALL_ENV_MODEL_ADAPTER_HPP

#include "data/teamdata.hpp"
#include "model/team.hpp"

// Bridges static domain descriptions to the legacy profile/formation inputs.
// Neither model/ nor data/ depends on the environment's composition logic.
TeamCreationData ToTeamCreationData(const football::model::Team& team,
                                    int database_id);

#endif  // FOOTBALL_ENV_MODEL_ADAPTER_HPP

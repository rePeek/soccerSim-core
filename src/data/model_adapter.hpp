#ifndef FOOTBALL_DATA_MODEL_ADAPTER_HPP
#define FOOTBALL_DATA_MODEL_ADAPTER_HPP

#include "data/teamdata.hpp"
#include "model/team.hpp"

// Legacy coordinate/role conversion belongs at the runtime-data boundary, not
// in the static model or the environment's lifecycle implementation.
std::vector<FormationEntry> ToLegacyFormation(
    const football::model::Formation& formation);
TeamCreationData ToTeamCreationData(
    const football::model::Team& team, int database_id,
    const std::vector<FormationEntry>& initial_formation);

#endif  // FOOTBALL_DATA_MODEL_ADAPTER_HPP

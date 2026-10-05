#include "data/model_adapter.hpp"

#include "data/default_teams.hpp"

std::vector<FormationEntry> ToLegacyFormation(
    const football::model::Formation& formation) {
  std::vector<FormationEntry> result;
  result.reserve(formation.size());
  for (const football::model::FormationEntry& entry : formation) {
    result.emplace_back(entry.position.x, entry.position.y, entry.role,
                        entry.lazy, entry.controllable);
  }
  return result;
}

TeamCreationData ToTeamCreationData(
    const football::model::Team& team, int database_id,
    const std::vector<FormationEntry>& initial_formation) {
  TeamCreationData data;
  data.database_id = database_id;
  data.name = team.name;
  // Resolve legacy empty-roster defaults here, before runtime identity
  // validation and before PlayerData consumes any RNG. No ID allocation.
  data.players = team.players.empty()
      ? (database_id == kHomeTeamDatabaseId
             ? football::data::MakeDefaultHomeTeam()
             : football::data::MakeDefaultAwayTeam()).players
      : team.players;
  data.formation = initial_formation;
  return data;
}

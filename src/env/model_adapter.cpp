#include "env/model_adapter.hpp"

TeamCreationData ToTeamCreationData(const football::model::Team& team,
                                    int database_id) {
  TeamCreationData data;
  data.database_id = database_id;
  data.name = team.name;
  data.formation.reserve(team.formation.size());
  for (const football::model::FormationEntry& entry : team.formation) {
    data.formation.emplace_back(entry.position.x, entry.position.y, entry.role,
                                entry.lazy, entry.controllable);
  }
  data.player_ids.reserve(team.players.size());
  for (const football::model::Player& player : team.players) {
    data.player_ids.push_back(player.database_id);
  }
  return data;
}

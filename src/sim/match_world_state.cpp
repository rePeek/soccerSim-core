#include "sim/match_world_state.hpp"

#include "sim/match.hpp"
#include "sim/player/player.hpp"
#include "sim/team.hpp"

WorldState BuildWorldState(const Match& match) {
  WorldState world;
  world.tick = match.GetActualTime_ms() / 10;
  world.ball_position = match.GetBall()->Predict(0);
  for (int team_id = 0; team_id < 2; ++team_id) {
    std::vector<Player*> players;
    match.GetTeam(team_id)->GetAllPlayers(players);
    for (Player* player : players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      world.players.push_back(WorldPlayerState{
          static_cast<PlayerId>(player->GetStableID()),
          static_cast<TeamId>(team_id), state.position, state.velocity,
          state.facing, player->IsActive(), player->HasPossession()});
    }
  }
  return world;
}

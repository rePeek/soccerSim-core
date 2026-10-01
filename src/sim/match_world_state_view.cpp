#include "sim/match_world_state_view.hpp"

#include "sim/match.hpp"
#include "sim/player/player.hpp"
#include "sim/team.hpp"

MatchWorldStateView::MatchWorldStateView(Match& match) {
  tick_ = match.GetActualTime_ms() / 10;
  ball_position_ = match.GetBall()->Predict(0);
  for (int team_id = 0; team_id < 2; ++team_id) {
    std::vector<Player*> players;
    match.GetTeam(team_id)->GetAllPlayers(players);
    for (Player* player : players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      players_.push_back(WorldPlayerState{
          static_cast<PlayerId>(player->GetStableID()),
          static_cast<TeamId>(team_id), state.position, state.velocity,
          state.facing, player->IsActive(), player->HasPossession()});
    }
  }
}

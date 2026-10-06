#include "sim/match_world_state.hpp"

#include "sim/match.hpp"
#include "sim/player/player.hpp"
#include "sim/team.hpp"

WorldState BuildWorldState(const Match& match) {
  WorldState world;
  world.tick = match.GetActualTime_ms() / 10;
  world.ball_position = match.GetBall()->Predict(0);
  world.ball_velocity = match.GetBall()->GetMovement();
  world.pitch = match.pitch();
  world.in_play = match.IsInPlay();
  world.in_set_piece = match.IsInSetPiece();
  const auto &restart = match.GetReferee()->GetBuffer();
  if (restart.active) {
    world.restart = restart.desiredSetPiece;
    if (restart.taker) world.restart_taker = restart.taker->GetID();
  }
  if (Player *retainer = match.GetBallRetainer()) world.ball_retainer = retainer->GetID();
  // Match stores the ball and first roster in its processing frame between
  // ticks. Policy inputs and outputs always use the home pitch frame.
  const bool reversed = match.options().reverse_team_processing;
  if (reversed) { world.ball_position.Mirror(); world.ball_velocity.Mirror(); }
  for (int team_id = 0; team_id < 2; ++team_id) {
    world.teams[team_id].score = match.GetScore(team_id);
    const auto &requests = match.GetTeam(team_id)->GetTacticalState();
    const auto now = match.GetActualTime_ms();
    const auto remaining = [now](unsigned long deadline) -> std::uint64_t {
      return deadline > now ? deadline - now : 0;
    };
    std::vector<Player*> players;
    match.GetTeam(team_id)->GetAllPlayers(players);
    for (Player* player : players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      WorldPlayerState observed{player->GetID(), player->GetTeam()->GetTeamSide(),
          state.position, state.velocity, state.facing,
          player->IsActive(), player->HasPossession()};
      const int static_side = team_id == 0 ? -1 : 1;
      if (player->GetTeam()->GetDynamicSide() != static_side) {
        observed.position.Mirror(); observed.velocity.Mirror(); observed.facing.Mirror();
      }
      observed.externally_controlled = player->ExternalControllerActive();
      observed.lazy = player->GetFormationEntry().lazy;
      observed.max_speed = player->GetMaxVelocity();
      if (observed.active) {
        if (requests.attacking_runner == player)
          observed.attacking_run_remaining_ms = remaining(requests.attacking_run_until_ms);
        if (requests.pressure_player == player) {
          observed.pressure_remaining_ms = remaining(requests.pressure_until_ms);
          if (observed.pressure_remaining_ms > 0) {
            if (Player *mark = player->GetManMarking(); mark && mark->IsActive())
              observed.marking_target = mark->GetID();
          }
        }
        if (player->GetFormationEntry().role == e_PlayerRole_GK)
          observed.keeper_rush_remaining_ms = remaining(requests.keeper_rush_until_ms);
      }
      world.players.push_back(observed);
    }
  }
  return world;
}

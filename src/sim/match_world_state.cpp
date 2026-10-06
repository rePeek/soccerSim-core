#include "sim/match_world_state.hpp"

#include "sim/match.hpp"
#include "sim/pitch_frame.hpp"
#include "sim/player/player.hpp"
#include "sim/team.hpp"

WorldState BuildWorldState(const Match& match) {
  WorldState world;
  world.tick = match.GetTimelineTick().value;
  world.phase = match.GetMatchPhase();
  world.match_time_ms = match.GetMatchTime_ms();
  world.reset_sequence = match.GetResetSequence();
  world.simulation_epoch = match.GetObservationEpoch();
  const auto ball_frame = ToHomePitchFrame(match);
  world.ball_position = ball_frame.Position(match.GetBall()->Predict(0));
  world.ball_velocity = ball_frame.Direction(match.GetBall()->GetMovement());
  world.pitch = match.pitch();
  world.in_play = match.IsInPlay();
  world.in_set_piece = match.IsInSetPiece();
  const auto &restart = match.GetReferee()->GetBuffer();
  if (restart.active) {
    world.restart = restart.desiredSetPiece;
    if (restart.taker) world.restart_taker = restart.taker->GetID();
  }
  if (Player *retainer = match.GetBallRetainer()) world.ball_retainer = retainer->GetID();
  for (int team_id = 0; team_id < 2; ++team_id) {
    world.teams[team_id].score = match.GetScore(team_id);
    const auto player_frame = ToHomePitchFrame(*match.GetTeam(team_id));
    std::vector<Player*> players;
    match.GetTeam(team_id)->GetAllPlayers(players);
    for (Player* player : players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      WorldPlayerState observed{player->GetID(), player->GetTeam()->GetTeamSide(),
          player_frame.Position(state.position), player_frame.Direction(state.velocity),
          player_frame.Direction(state.facing), player->IsActive(), player->HasPossession()};
      observed.lazy = player->GetFormationEntry().lazy;
      observed.max_speed = player->GetMaxVelocity();
      world.players.push_back(observed);
    }
  }
  return world;
}

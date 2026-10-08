#include "sim/observation/world_state_builder.hpp"

#include "football/ball/ball.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/player/player.hpp"
#include "sim/rules/referee.hpp"
#include "sim/team/team.hpp"

WorldState BuildWorldState(const WorldStateSource& s) {
  WorldState world;
  world.tick = s.now.value;
  world.phase = s.phase;
  world.regulation_time = s.regulation_time;
  world.ball_in_play_time = s.ball_in_play_time;
  world.half_underway = s.half_underway;
  world.ball_in_play = s.ball_in_play;
  world.reset_sequence = s.reset_sequence;

  const Team& first_roster = s.first_team == 0 ? s.home : s.away;
  const auto ball_frame = ToHomePitchFrame(first_roster);
  world.ball_position = ball_frame.Position(s.ball.Predict(0));
  world.ball_velocity = ball_frame.Direction(s.ball.GetMovement());
  world.pitch = s.pitch;
  world.in_play = s.in_play;
  world.in_set_piece = s.in_set_piece;

  const auto& restart = s.referee.GetBuffer();
  if (restart.active) {
    world.restart = restart.desiredSetPiece;
    if (restart.taker) world.restart_taker = restart.taker->GetID();
    world.restart_pending = s.referee.RestartNeedsSimulation();
  }
  if (s.ball_retainer) world.ball_retainer = s.ball_retainer->GetID();

  Team* rosters[2] = {&s.home, &s.away};
  const int score[2] = {s.score_home, s.score_away};
  for (int team_id = 0; team_id < 2; ++team_id) {
    world.teams[team_id].score = score[team_id];
    const auto player_frame = ToHomePitchFrame(*rosters[team_id]);
    std::vector<Player*> players;
    rosters[team_id]->GetAllPlayers(players);
    for (Player* player : players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      WorldPlayerState observed{player->GetID(), player->GetTeam()->GetTeamSide(),
          player_frame.Position(state.position), player_frame.Direction(state.velocity),
          player_frame.Direction(state.facing), player->IsActive(), player->HasPossession()};
      observed.lazy = player->GetFormationEntry().lazy;
      observed.max_speed = player->GetMaxVelocity();
      observed.restart_target = s.referee.GetRestartTarget(player);
      world.players.push_back(observed);
    }
  }
  return world;
}
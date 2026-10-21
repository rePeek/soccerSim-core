#include "observation/world_state_builder.hpp"

#include <cstddef>
#include <cstdint>

#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "referee/referee.hpp"

WorldState BuildWorldState(const WorldStateSource& s) {
  WorldState world;
  world.tick = s.now.value;
  world.phase = s.phase;
  world.regulation_time = s.regulation_time;
  world.ball_in_play_time = s.ball_in_play_time;
  world.half_underway = s.half_underway;
  world.ball_in_play = s.ball_in_play;
  world.reset_sequence = s.reset_sequence;

  // Observation emits the single authoritative Stadium Frame.
  world.ball_position = s.ball.state().position;
  world.ball_velocity = s.ball.state().velocity;
  // P21.2: the runtime Ball model owns the look-ahead; the AI only reads it.
  for (std::size_t i = 0; i < kWorldBallPredictionCount; ++i) {
    const football::sim::TickSpan horizon{
        static_cast<std::uint64_t>(kWorldBallPredictionHorizons_ms[i]) /
        football::sim::kMillisecondsPerTick};
    world.ball_prediction[i] = s.ball.Predict(horizon).position;
  }
  world.pitch = s.pitch;
  world.in_play = s.in_play;
  world.in_set_piece = s.in_set_piece;

  const football::sim::RestartContext& restart = s.restart;
  if (restart.active) {
    world.restart = restart.kind;
    if (restart.taker) world.restart_taker = restart.taker->GetID();
    world.restart_pending = restart.NeedsSimulation();
  }
  if (s.ball_retainer) world.ball_retainer = s.ball_retainer->GetID();

  const football::sim::TeamView* team_views[2] = {&s.home, &s.away};
  const int score[2] = {s.score_home, s.score_away};
  for (int team_id = 0; team_id < 2; ++team_id) {
    world.teams[team_id].score = score[team_id];
    world.teams[team_id].has_possession = team_views[team_id]->HasPossession();
    world.teams[team_id].fading_possession_amount =
        team_views[team_id]->fading_possession_amount();
    world.teams[team_id].time_to_ball_ms =
        team_views[team_id]->TimeNeededToGetToBall_ms();
    world.teams[team_id].defending_direction =
        team_views[team_id]->defending_direction;
    for (Player* player : team_views[team_id]->players) {
      const PlayerKinematicState& state = player->GetKinematicState();
      WorldPlayerState observed{player->GetID(), player->GetTeamSide(),
          state.position, state.velocity, state.GetFacing(), player->IsActive(),
          player->HasPossession()};
      observed.lazy = player->GetFormationEntry().lazy;
      observed.max_speed = player->GetMaxVelocity();
      observed.role = player->GetFormationEntry().role;
      observed.fatigue_inv = player->GetFatigueFactorInv();
      observed.time_to_ball_ms = player->GetTimeNeededToGetToBall_ms();
      observed.short_pass_skill =
          player->GetStat(football::player::PlayerStat::technical_shortpass);
      observed.workrate =
          player->GetStat(football::player::PlayerStat::mental_workrate);
      observed.restart_target = football::sim::RestartTargetFor(s.restart, player->GetID());
      world.players.push_back(observed);
    }
  }
  return world;
}
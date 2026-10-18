// TACT-7: deterministic coach scheduling and a synchronous mock agent loop with
// no network. Stale revisions and budgets are enforced by real code.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "ai/ai_engine.hpp"
#include "ai/coach/context_collector.hpp"
#include "ai/coach/heuristic_coach.hpp"
#include "ai/coach/scheduler.hpp"
#include "football/ai/coach_policy.hpp"

namespace {

using football::ai::CoachCommand;
using football::ai::CoachCommandEnvelope;
using football::ai::CoachContext;
using football::ai::CoachErrorCode;
using football::ai::CoachScheduler;
using football::ai::CoachSchedulerConfig;
using football::ai::ContextCollector;
using football::ai::HeuristicCoachPolicy;
using football::ai::TacticalBoard;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "sched";
  constexpr e_PlayerRole kRoles[kPlayers] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < kPlayers; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(definition);
    football::team::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.3f : 0.3f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

const football::pitch::Pitch& Pitch() {
  static const football::pitch::Pitch pitch;
  return pitch;
}

WorldState MakeWorld(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away) {
  WorldState world;
  world.phase = MatchPhase::SecondHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = football::math::Vector3(0.f, 0.f, 0.11f);
  world.pitch = Pitch();
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 1}}};
  world.teams[0].has_possession = true;
  auto add = [&](const football::sim::TeamSetup& team, TeamSide side, float sign) {
    for (int i = 0; i < kPlayers; ++i) {
      WorldPlayerState player;
      player.id = team.players[i].id;
      player.side = side;
      player.position = football::math::Vector3(sign * (2.f + 4.f * i), 0.f, 0.f);
      player.facing = football::math::Vector3(sign, 0.f, 0.f);
      player.active = true;
      player.max_speed = 8.f;
      player.role = team.tactical_formation[i].role;
      world.players.push_back(player);
    }
  };
  add(home, TeamSide::Home, -1.f);
  add(away, TeamSide::Away, 1.f);
  return world;
}

CoachContext ContextAt(std::uint64_t tick, TeamSide self) {
  CoachContext context;
  context.self = self;
  context.observed_tick = tick;
  context.phase = MatchPhase::SecondHalf;
  context.self_score = 0;
  context.opponent_score = 1;
  return context;
}

}  // namespace

TEST_CASE("the scheduler respects debounce, period and budget",
          "[ai][tactical][scheduler]") {
  CoachScheduler scheduler;
  CHECK(scheduler.ShouldRun(ContextAt(0, TeamSide::Home), false));

  scheduler.OnTurnRun(0, false);
  CHECK_FALSE(scheduler.ShouldRun(ContextAt(10000, TeamSide::Home), false));
  CHECK(scheduler.ShouldRun(ContextAt(30000, TeamSide::Home), false));
  scheduler.OnTurnRun(30000, false);
  CHECK(scheduler.ShouldRun(ContextAt(60000, TeamSide::Home), false));

  CoachSchedulerConfig config;
  config.debounce_ticks = 100;
  config.event_cooldown_ticks = 1000;
  config.max_calls = 2;
  CoachScheduler budgeted(config);
  CHECK(budgeted.ShouldRun(ContextAt(0, TeamSide::Home), false));
  budgeted.OnTurnRun(0, false);
  CHECK(budgeted.ShouldRun(ContextAt(100, TeamSide::Home), true));
  budgeted.OnTurnRun(100, true);
  CHECK_FALSE(budgeted.ShouldRun(ContextAt(500, TeamSide::Home), true));  // cooldown
  CHECK_FALSE(budgeted.ShouldRun(ContextAt(5000, TeamSide::Home), false));  // budget
  budgeted.Reset();
  CHECK(budgeted.CallsUsed() == 0);
}

TEST_CASE("the heuristic coach is a NoOp when the score is level",
          "[ai][tactical][scheduler]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  world.teams[0].score = 1;  // level
  world.teams[1].score = 1;
  ContextCollector collector(TeamSide::Home);
  collector.OnTick(world, {});
  auto board = football::ai::MakeTacticalBoardV2(home, TeamSide::Home, Pitch());
  const auto context = collector.Build(world, board);
  HeuristicCoachPolicy coach;
  std::vector<CoachCommand> commands;
  coach.RunTurn(context, commands);
  CHECK(commands.empty());
}

TEST_CASE("a synchronous mock coach loop edits once and rejects stale writes",
          "[ai][tactical][scheduler]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  football::ai::AIConfig config;
  config.initial_tactics[0] =
      football::ai::MakeTacticalBoardV2(home, TeamSide::Home, Pitch());
  config.initial_tactics[1] =
      football::ai::MakeTacticalBoardV2(away, TeamSide::Away, Pitch());
  football::ai::AIEngine engine(home, away, Pitch(), config);

  ContextCollector collector(TeamSide::Home);
  CoachSchedulerConfig scheduler_config;
  scheduler_config.debounce_ticks = 100;
  CoachScheduler scheduler(scheduler_config);
  HeuristicCoachPolicy coach;
  WorldState world = MakeWorld(home, away);
  std::uint64_t next_id = 0;

  auto turn = [&](std::uint64_t tick, bool event_triggered) {
    world.tick = tick;
    collector.OnTick(world, engine.ActiveShouts(TeamSide::Home));
    const CoachContext context =
        collector.Build(world, engine.tactics(TeamSide::Home));
    if (!scheduler.ShouldRun(context, event_triggered)) return;
    std::vector<CoachCommand> commands;
    coach.RunTurn(context, commands);
    for (CoachCommand& command : commands) {
      CoachCommandEnvelope envelope;
      envelope.id = ++next_id;
      envelope.observed_tick = context.observed_tick;
      envelope.expires_at_tick = context.observed_tick + 1000;
      envelope.expected_board_revision = context.board_revision;
      envelope.command = std::move(command);
      engine.SubmitCoachCommand(envelope);
    }
    scheduler.OnTurnRun(tick, event_triggered);
    PlayerControlSet output;
    engine.Update(world, output);
  };

  turn(0, false);
  const std::uint64_t revision_after_first = engine.tactics(TeamSide::Home).revision;
  CHECK(revision_after_first > 0);
  CHECK(engine.tactics(TeamSide::Home).advanced->in_possession.tempo == 0.70f);

  // Second turn: the plan already matches the target, so no new writes.
  turn(20000, false);
  CHECK(engine.tactics(TeamSide::Home).revision == revision_after_first);

  // A stale, hand-built command is rejected and logged, not applied.
  football::ai::SetTeamInstruction stale;
  stale.side = TeamSide::Home;
  stale.key = football::ai::InstructionKey::Tempo;
  stale.value = 0.1f;
  CoachCommandEnvelope envelope;
  envelope.id = 999;
  envelope.observed_tick = world.tick;
  envelope.expires_at_tick = world.tick + 1000;
  envelope.expected_board_revision = 0;  // stale
  envelope.command = stale;
  REQUIRE(engine.SubmitCoachCommand(envelope));
  PlayerControlSet output;
  engine.Update(world, output);
  CHECK(engine.tactics(TeamSide::Home).revision == revision_after_first);
  CHECK(engine.CoachLog().back().result.error == CoachErrorCode::StaleRevision);

  // The context exposes only the coach's own plan.
  const auto context = collector.Build(world, engine.tactics(TeamSide::Home));
  REQUIRE(context.current_tactics.has_value());
  CHECK(context.current_tactics->in_possession.tempo ==
        engine.tactics(TeamSide::Home).advanced->in_possession.tempo);
}

// RFE-6: the autonomous coach loop wired through AIEngine.
// Observation + formal MatchEvents -> ContextCollector -> CoachScheduler ->
// HeuristicCoachPolicy -> CoachCommandEnvelope -> CoachRuntime -> TacticalBoard.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "ai/ai_engine.hpp"
#include "ai_test_world.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/coach_command.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/ai/tactical_plan.hpp"
#include "football/event/match_event.hpp"

namespace {

using football::ai::AIEngine;
using football::ai::AIConfig;
using football::ai::TacticalBoard;
using football::ai::TacticalPlan;
using football::ai::TransitionOnLoss;
using football::ai::TransitionOnWin;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;
using football::sim::TeamSide;
using football::sim::Tick;

AIConfig CoachConfig(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away) {
  AIConfig config;
  config.coach_enabled = true;
  config.initial_tactics[0] =
      football::ai::MakeTacticalBoard(home, TeamSide::Home, football::pitch::Pitch{});
  config.initial_tactics[1] =
      football::ai::MakeTacticalBoard(away, TeamSide::Away, football::pitch::Pitch{});
  return config;
}

WorldState TrailingSecondHalf() {
  WorldState world = MakeWorld(MakeTeam(0), MakeTeam(100));
  world.phase = MatchPhase::SecondHalf;
  world.teams[0].score = 0;
  world.teams[1].score = 1;
  return world;
}

}  // namespace

TEST_CASE("the coach loop runs a full heuristic turn and commits it next tick",
          "[ai][coach][loop]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = TrailingSecondHalf();
  AIEngine engine(home, away, football::pitch::Pitch{}, CoachConfig(home, away));
  PlayerControlSet output;

  // Turn observes at 1000 and submits; commands commit at the next Update.
  world.tick = 1000;
  engine.ConsumeMatchEvents({});
  engine.Update(world, output);
  world.tick = 1001;
  engine.Update(world, output);

  const TacticalPlan& plan = engine.tactics(TeamSide::Home).plan;
  REQUIRE(plan.in_possession.tempo == 0.70f);
  REQUIRE(plan.in_possession.attacking_bias == 0.75f);
  REQUIRE(plan.in_possession.risk_taking == 0.65f);
  REQUIRE(plan.out_of_possession.defensive_line == 0.62f);
  REQUIRE(plan.out_of_possession.line_of_engagement == 0.70f);
  REQUIRE(plan.out_of_possession.pressing_intensity == 0.68f);
  REQUIRE(plan.transition.on_win == TransitionOnWin::Counter);
  REQUIRE(plan.transition.on_loss == TransitionOnLoss::CounterPress);
  // The whole turn is one transaction: every command applied, not just the first.
  REQUIRE(engine.tactics(TeamSide::Home).revision >= 8);
  REQUIRE(engine.CoachCalls(TeamSide::Home) == 1);
  for (const auto& record : engine.CoachLog()) {
    if (record.side == TeamSide::Home) REQUIRE(record.result.accepted);
  }
}

TEST_CASE("a later coach turn with no new state does not rewrite the board",
          "[ai][coach][loop]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = TrailingSecondHalf();
  AIEngine engine(home, away, football::pitch::Pitch{}, CoachConfig(home, away));
  PlayerControlSet output;

  world.tick = 1000;
  engine.Update(world, output);
  world.tick = 1001;
  engine.Update(world, output);
  const std::uint64_t revision = engine.tactics(TeamSide::Home).revision;
  REQUIRE(revision > 0);

  // Far past the debounce: the plan already matches the target, so the turn is
  // a NoOp and the board revision does not move.
  world.tick = 100000;
  engine.Update(world, output);
  world.tick = 100001;
  engine.Update(world, output);
  REQUIRE(engine.tactics(TeamSide::Home).revision == revision);
  REQUIRE(engine.CoachCalls(TeamSide::Home) == 2);
}

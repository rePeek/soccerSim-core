// REMOVE-1: the player heuristic layer is gone. With a valid plan, a live ball
// and possession, AIEngine emits no player control until the Intent Runtime
// exists. Absent control is the simulation's neutral idle execution; there is
// no hidden fallback to an old policy.
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_engine.hpp"
#include "ai_test_world.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/tactical_board.hpp"

namespace {

using football::ai::AIEngine;
using football::ai::AIConfig;
using football::ai::MakeTacticalBoard;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;
using football::sim::TeamSide;

AIConfig MakeConfig(const football::sim::TeamSetup& home,
                    const football::sim::TeamSetup& away) {
  AIConfig config;
  config.initial_tactics[0] =
      MakeTacticalBoard(home, TeamSide::Home, football::pitch::Pitch{});
  config.initial_tactics[1] =
      MakeTacticalBoard(away, TeamSide::Away, football::pitch::Pitch{});
  return config;
}

}  // namespace

TEST_CASE("AIEngine emits no autonomous control without a tactical intent",
          "[ai][neutral]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  // A live, in-play possession state is exactly what the old heuristics needed
  // to start chasing, passing or shooting.
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.0f;
  world.ball_retainer = home.players[5].id;
  for (WorldPlayerState& player : world.players)
    player.has_possession = player.id == home.players[5].id;

  AIEngine engine(home, away, football::pitch::Pitch{}, MakeConfig(home, away));
  PlayerControlSet output;
  engine.Update(world, output);

  CHECK(output.controls().empty());
}

TEST_CASE("AIEngine fully replaces a reused control buffer",
          "[ai][neutral]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  const WorldState world = MakeWorld(home, away);
  AIEngine engine(home, away, football::pitch::Pitch{}, MakeConfig(home, away));

  PlayerControlSet output;
  PlayerControl stale;
  stale.move_direction = football::math::Vector3(1.f, 0.f, 0.f);
  stale.desired_speed = 7.0f;
  output.Set(home.players[0].id, stale);
  REQUIRE(output.controls().size() == 1);

  engine.Update(world, output);

  CHECK(output.controls().empty());
}

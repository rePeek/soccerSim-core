#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "app/fixtures/default_teams.hpp"
#include "gameenv.hpp"

#ifdef FOOTBALL_AI_DEFAULT_AI_HPP
#error "gameenv public header must not expose the concrete policy implementation"
#endif

namespace {
GameEnv MakeGame(MatchOptions options = {}) {
  return {football::app::fixtures::MakeDefaultHomeTeam(),
          football::app::fixtures::MakeDefaultAwayTeam(),
          football::model::MakeLegacyPitch(), options, {}};
}
}
TEST_CASE("the CLI composition starts and exposes secondary owning telemetry", "[app][cli]") {
  auto game = MakeGame(); game.Start();
  const auto initial = game.Observe();
  REQUIRE(initial.players.size() == 22);
  REQUIRE(initial.phase == MatchPhase::PreMatch);
  for (int tick = 0; tick < 10; ++tick) game.Step();
  REQUIRE(game.Observe().tick == 10); REQUIRE(initial.tick == 0);
  REQUIRE_THROWS_AS(game.Result(), std::logic_error);
}
TEST_CASE("the CLI composition reports declared identities", "[app][cli]") {
  auto game = MakeGame(); game.Start();
  const auto world = game.Observe();
  for (std::size_t i = 0; i < 11; ++i) {
    REQUIRE(world.players[i].id == i);
    REQUIRE(world.players[i].side == football::model::TeamSide::Home);
    REQUIRE(world.players[11 + i].id == 11 + i);
    REQUIRE(world.players[11 + i].side == football::model::TeamSide::Away);
  }
}
TEST_CASE("the CLI runner ends autonomously with a final result", "[app][cli]") {
  MatchOptions options; options.half_duration_ms = 1800;
  auto game = MakeGame(options); game.Start();
  std::uint64_t steps = 0;
  while (!game.Finished()) { game.Step(); REQUIRE(++steps < 1000); }
  const auto result = game.Result();
  REQUIRE(result.duration_ticks == steps);
  REQUIRE(game.Observe().phase == MatchPhase::Finished);
  REQUIRE(result.home_score == game.Observe().teams[0].score);
  REQUIRE(result.away_score == game.Observe().teams[1].score);
  game.Stop(); REQUIRE_FALSE(game.Finished());
  REQUIRE_THROWS_AS(game.Result(), std::logic_error);
  game.Start(); while (!game.Finished()) game.Step();
  REQUIRE(game.Result() == result);
}

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "model/pitch.hpp"

#ifdef FOOTBALL_AI_DEFAULT_AI_HPP
#error "env public header must not expose the concrete policy implementation"
#endif

using football::ai::PlannedPlayerRole;

namespace {

namespace fixtures = football::app::fixtures;
namespace model = football::model;

// The exact composition the CLI builds, so the end-to-end assertions below
// track what `football_app` actually runs.
GameEnv MakeDefaultEnvironment() {
  return GameEnv{fixtures::MakeDefaultHomeTeam(),
                 fixtures::MakeDefaultAwayTeam(), model::MakeLegacyPitch()};
}

}  // namespace

TEST_CASE("the CLI composition starts, ticks and observes", "[app][cli]") {
  GameEnv environment = MakeDefaultEnvironment();
  environment.start_game();

  const WorldState initial = environment.observe();
  CHECK(initial.tick == 0);
  REQUIRE(initial.players.size() == 22);
  CHECK(std::isnan(initial.ball_position.coords[0]) == false);

  for (int tick = 0; tick < 10; ++tick) environment.step();
  const WorldState world = environment.observe();
  CHECK(world.tick == 10);
  CHECK(world.players.size() == initial.players.size());

  environment.stop_game();
}

TEST_CASE("the CLI composition reports the declared identities",
          "[app][cli]") {
  GameEnv environment = MakeDefaultEnvironment();
  environment.start_game();
  const WorldState world = environment.observe();

  REQUIRE(world.players.size() == 22);
  for (std::size_t i = 0; i < 11; ++i) {
    CHECK(world.players[i].id == static_cast<model::PlayerId>(i));
    CHECK(world.players[i].side == model::TeamSide::Home);
    CHECK(world.players[11 + i].id == static_cast<model::PlayerId>(11 + i));
    CHECK(world.players[11 + i].side == model::TeamSide::Away);
  }

  environment.stop_game();
}

TEST_CASE("the CLI composition is reproducible across restarts", "[app][cli]") {
  GameEnv environment = MakeDefaultEnvironment();
  environment.start_game();
  for (int tick = 0; tick < 25; ++tick) environment.step();
  const WorldState first = environment.observe();

  environment.reset_game();
  for (int tick = 0; tick < 25; ++tick) environment.step();
  const WorldState second = environment.observe();

  REQUIRE(first.players.size() == second.players.size());
  CHECK(first.tick == second.tick);
  for (std::size_t i = 0; i < first.players.size(); ++i) {
    CHECK(first.players[i].id == second.players[i].id);
    CHECK(first.players[i].position.coords[0] ==
          second.players[i].position.coords[0]);
    CHECK(first.players[i].position.coords[1] ==
          second.players[i].position.coords[1]);
  }

  environment.stop_game();
}

TEST_CASE("environment exposes persistent AI intent independently of sim lifecycle", "[env][ai]") {
  GameEnv environment = MakeDefaultEnvironment();
  GameEnv peer = MakeDefaultEnvironment();
  auto &board = environment.tactics(model::TeamSide::Home);
  REQUIRE(board.players.size() == 11);
  board.width = 0.9f;
  board.depth = 0.6f;
  board.players[7].role = PlannedPlayerRole::Forward;
  board.players[7].marking_target = 12;
  const auto anchor = board.players[7].formation_position;
  environment.start_game();
  for (int tick = 0; tick < 300; ++tick) environment.step();
  environment.reset_game();
  environment.step();
  environment.stop_game();
  environment.start_game();
  environment.step();
  REQUIRE(&environment.tactics(model::TeamSide::Home) == &board);
  const GameEnv &read_only = environment;
  REQUIRE(read_only.tactics(model::TeamSide::Home).width == 0.9f);
  REQUIRE(board.depth == 0.6f);
  REQUIRE(board.players[7].role == PlannedPlayerRole::Forward);
  REQUIRE(board.players[7].marking_target == 12);
  REQUIRE(board.players[7].formation_position == anchor);
  REQUIRE(environment.tactics(model::TeamSide::Away).width == 0.75f);
  REQUIRE(peer.tactics(model::TeamSide::Home).width == 0.75f);
}

TEST_CASE("environment façade routes requests without exposing its policy", "[env][ai]") {
  GameEnv environment = MakeDefaultEnvironment();
  REQUIRE_FALSE(environment.request_attacking_run(model::TeamSide::Home, 7));
  REQUIRE_FALSE(environment.request_team_pressure(model::TeamSide::Home));
  REQUIRE_FALSE(environment.request_keeper_rush(model::TeamSide::Home));
  auto &board = environment.tactics(model::TeamSide::Home);
  board.width = 0.9f;
  environment.start_game();
  for (int tick = 0; tick < 300; ++tick) environment.step();
  REQUIRE(environment.request_attacking_run(model::TeamSide::Home, 7));
  REQUIRE(environment.request_team_pressure(model::TeamSide::Home));
  REQUIRE(environment.request_keeper_rush(model::TeamSide::Home));
  REQUIRE_FALSE(environment.request_attacking_run(model::TeamSide::Home, 123456));
  REQUIRE(environment.request_attacking_run(model::TeamSide::Home, 7));
  environment.reset_game();
  const auto reset_world = environment.observe();
  REQUIRE(&board == &environment.tactics(model::TeamSide::Home));
  REQUIRE(board.width == 0.9f);
  for (int tick = 0; tick < 300; ++tick) environment.step();
  const auto after_reset = environment.observe();
  REQUIRE(environment.request_attacking_run(model::TeamSide::Home, 7));
  environment.stop_game();
  REQUIRE_FALSE(environment.request_keeper_rush(model::TeamSide::Home));
  environment.start_game();
  REQUIRE(environment.observe().simulation_epoch != reset_world.simulation_epoch);
  for (int tick = 0; tick < 300; ++tick) environment.step();
  const auto restarted = environment.observe();
  REQUIRE(restarted.simulation_epoch != after_reset.simulation_epoch);
  REQUIRE(after_reset.tick == restarted.tick);
  for (std::size_t i = 0; i < restarted.players.size(); ++i) {
    REQUIRE(after_reset.players[i].position == restarted.players[i].position);
    REQUIRE(after_reset.players[i].velocity == restarted.players[i].velocity);
  }
  REQUIRE(board.width == 0.9f);
}

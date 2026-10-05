#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "model/pitch.hpp"

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

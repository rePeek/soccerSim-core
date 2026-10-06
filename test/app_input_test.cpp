#include <array>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "app/input/grf/input.hpp"

namespace {
using blunted::Vector3;
using football::model::PlayerId;
using football::model::TeamSide;
using football::app::grf::Action;
using football::app::grf::Input;
using football::app::grf::TeamDecisionRequest;

struct Fixture {
  football::model::Team home, away;
  WorldState world;
  Fixture() {
    home.players.resize(3);
    home.players[0].id = 9; home.players[1].id = 8; home.players[2].id = 7;
    away.players.resize(1); away.players[0].id = 100;
    home.tactical_formation = {{{-1, 0}, e_PlayerRole_GK},
                              {{-0.3f, 0}, e_PlayerRole_CM}, {{0.2f, 0}, e_PlayerRole_CF}};
    world.tick = 100;
    world.in_play = true;
    for (const auto &[id, x] : std::array<std::pair<PlayerId, float>, 3>{{{9, -50}, {8, -10}, {7, -20}}}) {
      WorldPlayerState player;
      player.id = id; player.active = true; player.max_speed = 8.f;
      player.position = Vector3(x, 0, 0);
      world.players.push_back(player);
    }
    WorldPlayerState opponent;
    opponent.id = 100; opponent.side = TeamSide::Away; opponent.active = true;
    opponent.position = Vector3(10, 0, 0); opponent.max_speed = 8.f;
    world.players.push_back(opponent);
  }
};
}  // namespace

TEST_CASE("GRF wire numbers are bounded and sticky movement emits values", "[app][input]") {
  Fixture fixture;
  Input input(fixture.home, TeamSide::Home);
  TeamDecisionRequest requests;
  PlayerControlSet controls;
  for (int action = 0; action <= 32; ++action) {
    input.Reset();
    REQUIRE(input.Apply(action));
  }
  REQUIRE_FALSE(input.Apply(-1));
  REQUIRE_FALSE(input.Apply(33));
  REQUIRE_FALSE(input.Apply(std::numeric_limits<int>::max()));
  input.Reset();
  REQUIRE(input.Apply(Action::TopRight));
  REQUIRE(input.IsStickyActionActive(Action::TopRight));
  input.Update(fixture.world, controls, requests);
  REQUIRE(input.selected() == 8);
  REQUIRE(controls.controls().size() == 1);
  REQUIRE(controls.Get(8)->move_direction.coords[0] > 0.7f);
  REQUIRE(controls.Get(8)->move_direction.coords[1] > 0.7f);
  REQUIRE(controls.Get(8)->desired_speed == 5.f);
  input.Apply(Action::Sprint);
  input.Apply(Action::Idle);  // Idle does not release sticky state.
  input.Update(fixture.world, controls, requests);
  REQUIRE(controls.Get(8)->desired_speed == 8.f);
  input.Apply(Action::ReleaseSprint);
  input.Apply(Action::Dribble);
  input.Update(fixture.world, controls, requests);
  REQUIRE(controls.Get(8)->desired_speed == 3.5f);
  REQUIRE(controls.Get(8)->action == ControlAction::Dribble);
  input.Apply(Action::ReleaseDribble);
  input.Apply(Action::ReleaseDirection);
  input.Update(fixture.world, controls, requests);
  REQUIRE(controls.Get(8)->desired_speed == 0.f);
  REQUIRE_FALSE(input.IsStickyActionActive(Action::TopRight));
}

TEST_CASE("GRF kicks and tackles are one-shot executable actions", "[app][input]") {
  Fixture fixture;
  Input input(fixture.home, TeamSide::Home);
  TeamDecisionRequest requests;
  PlayerControlSet controls;
  const std::array actions{Action::LongPass, Action::HighPass, Action::ShortPass, Action::Shot, Action::Sliding};
  const std::array expected{ControlAction::LongPass, ControlAction::HighPass, ControlAction::ShortPass,
                            ControlAction::Shoot, ControlAction::Tackle};
  const std::array releases{Action::ReleaseLongPass, Action::ReleaseHighPass, Action::ReleaseShortPass,
                            Action::ReleaseShot, Action::ReleaseSliding};
  for (std::size_t i = 0; i < actions.size(); ++i) {
    input.Apply(actions[i]);
    input.Update(fixture.world, controls, requests);
    REQUIRE(controls.Get(8)->action == expected[i]);
    REQUIRE(controls.Get(8)->power == 0.6f);
    input.Update(fixture.world, controls, requests);
    REQUIRE(controls.Get(8)->action == ControlAction::None);
    input.Apply(actions[i]); input.Apply(releases[i]);
    input.Update(fixture.world, controls, requests);
    REQUIRE(controls.Get(8)->action == ControlAction::None);
  }
}

TEST_CASE("input selection switch deactivation and reserved slots stay outside sim", "[app][input]") {
  Fixture fixture;
  Input first(fixture.home, TeamSide::Home), second(fixture.home, TeamSide::Home);
  TeamDecisionRequest requests;
  PlayerControlSet controls;
  first.Update(fixture.world, controls, requests);
  REQUIRE(first.selected() == 8);
  const std::array<PlayerId, 1> reserved{8};
  second.Update(fixture.world, controls, requests, reserved);
  REQUIRE(second.selected() == 7);
  fixture.world.players[0].position = Vector3(0.1f, 0, 0); // Do not switch to the model keeper.
  first.Apply(Action::Switch);
  first.Update(fixture.world, controls, requests);
  REQUIRE(first.selected() == 7);
  first.Update(fixture.world, controls, requests);
  REQUIRE(first.selected() == 7);
  fixture.world.players[0].position = Vector3(-50, 0, 0);
  fixture.world.players[2].active = false;
  first.Update(fixture.world, controls, requests);
  REQUIRE(first.selected() == 8);
  REQUIRE_FALSE(first.Select(fixture.world, 100));
  REQUIRE_FALSE(first.Select(fixture.world, 7));
  fixture.world.players[0].active = fixture.world.players[1].active = false;
  first.Update(fixture.world, controls, requests);
  REQUIRE_FALSE(first.selected().has_value());
  REQUIRE(controls.controls().empty());
  REQUIRE_FALSE(requests.team_pressure);
}

TEST_CASE("closest initial subset binds roster order with full width descending identities", "[app][input]") {
  for (const auto side : {TeamSide::Home, TeamSide::Away}) {
    WorldState world;
    std::vector<PlayerId> eligible;
    for (std::size_t i = 0; i < 12; ++i) {
      WorldPlayerState player;
      player.id = 4000000000u - static_cast<PlayerId>(i);
      player.side = side; player.active = true;
      player.position = Vector3(20.f + i, 0, 0);
      world.players.push_back(player);
      if (i == 0 || i == 1 || i == 10) eligible.push_back(player.id);
    }
    world.players[10].position = Vector3(0.1f, 0, 0);
    world.players[1].position = Vector3(0.2f, 0, 0);
    world.players[0].position = Vector3(0.3f, 0, 0);
    const auto ids = football::app::grf::SelectInitialPlayers(world, side, eligible, 4);
    REQUIRE(ids == eligible);
    const auto nearest = football::app::grf::SelectInitialPlayers(world, side, eligible, 1);
    REQUIRE(nearest.size() == 1);
    REQUIRE(nearest[0] == world.players[10].id);
  }
}

TEST_CASE("GRF reads restart authority and emits no stopped actions", "[app][input]") {
  Fixture fixture;
  Input input(fixture.home, TeamSide::Home);
  TeamDecisionRequest requests;
  PlayerControlSet controls;
  fixture.world.in_set_piece = true;
  fixture.world.restart_taker = 7;
  input.Apply(Action::Shot);
  input.Apply(Action::TeamPressure);
  input.Apply(Action::KeeperRush);
  input.Update(fixture.world, controls, requests);
  REQUIRE(input.selected() == 7);
  REQUIRE(controls.Get(7)->action == ControlAction::Shoot);
  REQUIRE_FALSE(requests.team_pressure);
  REQUIRE_FALSE(requests.keeper_rush);
  fixture.world.in_play = false;
  input.Apply(Action::Shot);
  input.Update(fixture.world, controls, requests);
  REQUIRE(controls.Get(7)->action == ControlAction::None);
  REQUIRE(controls.Get(7)->desired_speed == 0.f);
  REQUIRE(fixture.world.restart_taker == 7);
  fixture.world.in_play = true;
  fixture.world.restart_taker = 100;
  input.Apply(Action::Shot);
  input.Update(fixture.world, controls, requests);
  REQUIRE(controls.Get(7)->action == ControlAction::None);
}

TEST_CASE("GRF emits team request values without any policy and clears every output frame", "[app][input]") {
  Fixture fixture;
  Input input(fixture.home, TeamSide::Home);
  TeamDecisionRequest requests;
  PlayerControlSet human;
  fixture.world.players[1].has_possession = true;
  input.Apply(Action::Switch);
  input.Apply(Action::TeamPressure);
  input.Apply(Action::KeeperRush);
  input.Update(fixture.world, human, requests);
  REQUIRE(input.selected() == 8);
  REQUIRE(requests.side == TeamSide::Home);
  REQUIRE(requests.attacking_run);
  REQUIRE(requests.team_pressure);
  REQUIRE(requests.pressure_excluded_player == 8);
  REQUIRE(requests.keeper_rush);
  REQUIRE(human.Get(8)->desired_speed == 0.f);
  const auto retained = requests;
  input.Update(fixture.world, human, requests);
  REQUIRE_FALSE(requests.attacking_run);  // Switch is one-shot, pressure/rush sticky.
  REQUIRE(requests.team_pressure);
  input.Apply(Action::ReleaseTeamPressure);
  input.Apply(Action::ReleaseKeeperRush);
  input.Update(fixture.world, human, requests);
  REQUIRE_FALSE(requests.team_pressure);
  REQUIRE_FALSE(requests.keeper_rush);
  REQUIRE_FALSE(requests.pressure_excluded_player.has_value());
  input.Apply(Action::BuiltinAI);
  input.Update(fixture.world, human, requests);
  REQUIRE(human.controls().empty());
  REQUIRE_FALSE(requests.attacking_run);
  REQUIRE_FALSE(requests.team_pressure);
  REQUIRE_FALSE(requests.keeper_rush);
  REQUIRE(retained.attacking_run);
  REQUIRE(retained.pressure_excluded_player == 8);
  input.Reset();
  REQUIRE_FALSE(input.selected().has_value());
}

TEST_CASE("input instances copies sides and reset do not share state", "[app][input]") {
  Fixture fixture;
  Input input(fixture.home, TeamSide::Home);
  input.Apply(Action::Right);
  input.Select(fixture.world, 7);
  auto copy = input;
  copy.Reset();
  REQUIRE(input.selected() == 7);
  REQUIRE(input.IsStickyActionActive(Action::Right));
  REQUIRE_FALSE(copy.IsStickyActionActive(Action::Right));
  Input away(fixture.away, TeamSide::Away);
  REQUIRE(away.Select(fixture.world, 100));
  REQUIRE_FALSE(away.Select(fixture.world, 7));
  PlayerControlSet controls;
  TeamDecisionRequest requests;
  away.Apply(Action::TeamPressure);
  away.Update(fixture.world, controls, requests);
  REQUIRE(requests.side == TeamSide::Away);
  REQUIRE(requests.pressure_excluded_player == 100);
}

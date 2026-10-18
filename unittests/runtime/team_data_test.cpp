#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

#include "football/runtime/active_lineup.hpp"
#include "football/runtime/match_config.hpp"
#include "runtime/team_setup_adapter.hpp"

using namespace football::sim;

namespace {
TeamSetup Setup() {
  TeamSetup setup;
  setup.name = "Team";
  for (auto id : {90u, 12u, 70u}) {
    football::player::PlayerDefinition player;
    player.id = id;
    setup.players.push_back(player);
  }
  return setup;
}
}

TEST_CASE("TeamSetup adapter creates static Team defaults", "[team][adapter]") {
  auto setup = Setup();
  auto prepared = PrepareTeam(setup);
  CHECK_FALSE(prepared.explicit_lineup);
  CHECK(prepared.team.name == setup.name);
  CHECK(prepared.team.players[0].id == 90);
  CHECK(prepared.team.lineup.starters[0] == 90);
  CHECK(prepared.team.lineup.starters[2] == 70);
  CHECK_FALSE(prepared.team.lineup.starters[3]);
  CHECK_FALSE(prepared.team.leadership.captain);
  CHECK(prepared.team.set_piece_orders.penalties.empty());
  CHECK(prepared.team.formations.presets.empty());

  setup.formation.resize(1);
  prepared = PrepareTeam(setup);
  CHECK_FALSE(prepared.team.lineup.starters[1]);
  CHECK(prepared.team.lineup.substitutes ==
        std::vector<football::player::PlayerId>{12, 70});

  setup = Setup();
  for (unsigned id = 100; id < 112; ++id) {
    football::player::PlayerDefinition player;
    player.id = id;
    setup.players.push_back(player);
  }
  prepared = PrepareTeam(setup);
  CHECK(prepared.team.lineup.starters[10] == 107);
  CHECK(prepared.team.lineup.substitutes.size() == 4);
}

TEST_CASE("TeamSetup adapter keeps explicit sparse lineup and construction mode",
          "[team][adapter]") {
  auto setup = Setup();
  setup.lineup.emplace();
  setup.lineup->starters[8] = 70;
  setup.lineup->starters[0] = 12;
  setup.lineup->substitutes = {90};
  auto prepared = PrepareTeam(setup);
  CHECK(prepared.explicit_lineup);
  CHECK(prepared.team.lineup.starters[8] == 70);
  CHECK_FALSE(prepared.team.lineup.starters[1]);

  SECTION("duplicate starter") { setup.lineup->starters[1] = 70; }
  SECTION("starter also substitute") { setup.lineup->substitutes.push_back(70); }
  SECTION("unknown identity") { setup.lineup->starters[1] = 400; }
  SECTION("duplicate definition") { setup.players[0].id = 12; }
  SECTION("invalid identity") {
    setup.players[0].id = football::player::kInvalidPlayerId;
  }
  CHECK_THROWS_AS(PrepareTeam(setup), std::invalid_argument);
}

TEST_CASE("Runtime slot vacancy and substitution preserve Team template identity",
          "[team][adapter]") {
  const auto team = PrepareTeam(Setup()).team;
  ActiveLineupState active{team.lineup.starters};
  VacateSlot(active, 12);
  CHECK_FALSE(active.slots[1]);
  CHECK(active.slots[2] == 70);
  ReplaceSlot(active, 1, 500);
  CHECK(active.slots[1] == 500);
  CHECK(team.lineup.starters[1] == 12);
  CHECK_THROWS_AS(ReplaceSlot(active, 2, 500), std::invalid_argument);
  CHECK_THROWS_AS(ReplaceSlot(active, 11, 40), std::invalid_argument);
}

TEST_CASE("Implicit TeamSetup keeps the legacy formation/player bound",
          "[team][adapter]") {
  auto setup = Setup();
  setup.formation.resize(4);
  CHECK_THROWS_AS(PrepareTeam(setup), std::invalid_argument);
  setup.lineup.emplace();
  setup.lineup->starters[0] = 90;
  CHECK_NOTHROW(PrepareTeam(setup));
}

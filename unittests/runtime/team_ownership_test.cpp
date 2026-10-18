#include <catch2/catch_test_macros.hpp>
#include "testing/simulation_access.hpp"
#include "runtime/formation_build.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;
namespace {
TeamSetup OwnershipSetup(unsigned first) {
  TeamSetup s;
  for (unsigned i = 0; i < 2; ++i) {
    football::player::PlayerDefinition p;
    p.id = first + i;
    p.height = 1.75f + i * .1f;
    s.players.push_back(p);
    s.formation.push_back({{-.7f + i * .4f, i * .3f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return s;
}
}
TEST_CASE("Team ownership preserves legacy formation adapter", "[team][ownership]") {
  auto s = OwnershipSetup(50);
  const auto legacy = BuildFormation(s);
  const auto migrated = BuildFormation(MakeTeam(s));
  REQUIRE(legacy.size() == migrated.size());
  for (std::size_t i = 0; i < legacy.size(); ++i) {
    CHECK(legacy[i].position == migrated[i].position);
    CHECK(legacy[i].start_position == migrated[i].start_position);
    CHECK(legacy[i].role == migrated[i].role);
    CHECK(legacy[i].lazy == migrated[i].lazy);
    CHECK(legacy[i].controllable == migrated[i].controllable);
  }
}
TEST_CASE("Team definitions remain final-address borrows through Stop and re-Init", "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  for (bool reverse : {false,true}) {
    auto home = OwnershipSetup(50);
    auto away = OwnershipSetup(500);
    options.reverse_team_processing = reverse;
    sim.Init(home,away,football::pitch::Pitch{},options);
    // Reallocation/destruction of caller inputs must not affect actor borrows.
    home.players.clear();
    home.players.shrink_to_fit();
    for (int side = 0; side < 2; ++side) {
      const auto& team = SimulationAccess::TeamOf(sim,static_cast<TeamSide>(side));
      const auto actors = SimulationAccess::RosterOf(sim,side).Players();
      REQUIRE(actors.size() == team.players.size());
      for (std::size_t i = 0; i < actors.size(); ++i) {
        CHECK(&actors[i]->GetModel() == &team.players[i]);
        CHECK(actors[i]->GetID() == (side == 0 ? 50 : 500) + i);
        CHECK(actors[i]->GetTeamSide() == static_cast<TeamSide>(side));
      }
    }
    CHECK_THROWS_AS(sim.Init(away,away,football::pitch::Pitch{},options),std::logic_error);
    CHECK(sim.Stop());
    CHECK_FALSE(sim.Stop());
  }
  auto invalid = OwnershipSetup(50);
  invalid.players[1].id = invalid.players[0].id;
  CHECK_THROWS_AS(sim.Init(invalid,OwnershipSetup(500),football::pitch::Pitch{},options),std::invalid_argument);
  CHECK(SimulationAccess::RosterOf(sim,0).Players().empty());
  CHECK_NOTHROW(sim.Init(OwnershipSetup(50),OwnershipSetup(500),football::pitch::Pitch{},options));
  CHECK(sim.Stop());
}
TEST_CASE("Failed Init destroys actors before definitions can be replaced", "[team][ownership]") {
  Simulation sim;
  MatchOptions options;
  // vector::max_size rejects this synchronously, after both rosters have been
  // constructed but before publication. No huge allocation or simulation loop.
  options.snapshot_capacity = std::numeric_limits<std::size_t>::max();
  CHECK_THROWS_AS(sim.Init(OwnershipSetup(50),OwnershipSetup(500),
      football::pitch::Pitch{},options),std::length_error);
  CHECK(SimulationAccess::RosterOf(sim,0).Players().empty());
  CHECK(SimulationAccess::RosterOf(sim,1).Players().empty());
  CHECK_FALSE(sim.Stop());
  options.snapshot_capacity = 8;
  REQUIRE_NOTHROW(sim.Init(OwnershipSetup(800),OwnershipSetup(900),
      football::pitch::Pitch{},options));
  const auto players = SimulationAccess::RosterOf(sim,0).Players();
  const auto& team = SimulationAccess::TeamOf(sim,TeamSide::Home);
  CHECK(&players.front()->GetModel() == &team.players.front());
  CHECK(players.front()->GetID() == 800);
  CHECK(sim.Stop());
}

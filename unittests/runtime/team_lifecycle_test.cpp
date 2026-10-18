#include <catch2/catch_test_macros.hpp>
#include "testing/simulation_access.hpp"

using namespace football::sim;
using football::sim::testing::SimulationAccess;
namespace {
TeamSetup LifecycleSetup(unsigned base) {
  TeamSetup s;
  for (unsigned i = 0; i < 3; ++i) {
    football::player::PlayerDefinition p;
    p.id = base + i;
    s.players.push_back(p);
    s.formation.push_back({{-.8f + i * .3f, i * .1f},
        i == 0 ? e_PlayerRole_GK : e_PlayerRole_CF});
  }
  return s;
}
void CheckStopped(Simulation& sim) {
  CHECK_FALSE(SimulationAccess::IsInitializedOf(sim));
  // TeamOf owns the only Player container; a stopped simulation has no Team.
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim,0),std::logic_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim,1),std::logic_error);
  CHECK_FALSE(SimulationAccess::DesignatedPlayerOf(sim));
  CHECK_FALSE(SimulationAccess::BallRetainerOf(sim));
  CHECK_FALSE(SimulationAccess::LastGoalScorerOf(sim));
  CHECK_FALSE(SimulationAccess::BallOf(static_cast<const Simulation&>(sim)));
  CHECK(sim.Snapshots().Empty());
  CHECK(sim.SnapshotMetadata().player_ids.empty());
  CHECK(sim.PlayerSlots().player_ids.empty());
  CHECK(sim.CommittedBakedPoses().empty());
  CHECK(SimulationAccess::RuleTouchesOf(sim).empty());
  CHECK(SimulationAccess::RecordedTouchesOf(sim).empty());
  CHECK_THROWS_AS(SimulationAccess::ClockOf(sim),std::logic_error);
  CHECK_THROWS_AS(SimulationAccess::TeamOf(sim,TeamSide::Home),std::logic_error);
  CHECK_THROWS_AS(sim.Observe(),std::logic_error);
  CHECK_THROWS_AS(sim.Step({}),std::logic_error);
  CHECK_FALSE(sim.Stop());
}
}
TEST_CASE("Partial Player construction failure takes the shared teardown path", "[team][ownership][lifecycle]") {
  Simulation sim;
  MatchOptions options; options.snapshot_capacity = 8;
  for (bool reverse : {false,true}) {
    options.reverse_team_processing = reverse;
    for (int failing_side : {0,1}) {
      std::array teams{LifecycleSetup(100),LifecycleSetup(500)};
      // ID/config validation succeeds. Body-profile construction throws only
      // after the preceding actor was constructed and activated. Either the
      // first or second processing roster can fail; no artificial test hook.
      teams[failing_side].players[1].height = 0;
      REQUIRE_THROWS_AS(sim.Init(teams[0],teams[1],football::pitch::Pitch{},options),
                        std::invalid_argument);
      CheckStopped(sim);
      const auto home = LifecycleSetup(100);
      const auto away = LifecycleSetup(500);
      Simulation fresh;
      REQUIRE_NOTHROW(sim.Init(home,away,football::pitch::Pitch{},options));
      fresh.Init(home,away,football::pitch::Pitch{},options);
      CHECK(SimulationAccess::RngOf(sim).engine() == SimulationAccess::RngOf(fresh).engine());
      const auto a = sim.Observe();
      const auto b = fresh.Observe();
      REQUIRE(a.players.size() == b.players.size());
      for (std::size_t i = 0; i < a.players.size(); ++i) {
        CHECK(a.players[i].id == b.players[i].id);
        CHECK(a.players[i].position == b.players[i].position);
        CHECK(a.players[i].active == b.players[i].active);
      }
      REQUIRE(sim.Stop());
      CheckStopped(sim);
    }
  }
}
TEST_CASE("Post-actor allocation failure and repeated Stop release all borrows", "[team][ownership][lifecycle]") {
  Simulation sim;
  CheckStopped(sim);
  MatchOptions options;
  options.snapshot_capacity = std::numeric_limits<std::size_t>::max();
  REQUIRE_THROWS_AS(sim.Init(LifecycleSetup(100),LifecycleSetup(500),
      football::pitch::Pitch{},options),std::length_error);
  CheckStopped(sim);
  options.snapshot_capacity = 8;
  sim.Init(LifecycleSetup(100),LifecycleSetup(500),football::pitch::Pitch{},options);
  REQUIRE(sim.Stop());
  CheckStopped(sim);
  // Also exercise destructor-driven cleanup with a fully live composition.
  REQUIRE_NOTHROW(sim.Init(LifecycleSetup(100),LifecycleSetup(500),
      football::pitch::Pitch{},options));
}

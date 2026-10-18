#include <catch2/catch_test_macros.hpp>

#include "testing/simulation_access.hpp"

namespace {
using football::sim::testing::SimulationAccess;
using namespace football::sim;
using namespace football::sim::event;

TeamSetup Team(football::player::PlayerId first) {
  TeamSetup team;
  for (int i = 0; i < 2; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + i;
    team.players.push_back(player);
  }
  team.tactical_formation = {{{-1, 0}, e_PlayerRole_GK}, {{0.4f, 0}, e_PlayerRole_CF}};
  return team;
}
void Init(Simulation& sim) {
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(Team(0), Team(2), football::pitch::Pitch{}, options, football::ball::BallModel{});
}
}  // namespace

TEST_CASE("Rulings: submit is deferred and commit drains exactly once", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& sink = SimulationAccess::RulingsOf(sim);
  sink.Submit(AwardGoalRuling{TeamSide::Away});
  REQUIRE(SimulationAccess::ScoreOf(sim, 1) == 0);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 1);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::ScoreOf(sim, 1) == 1);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::ScoreOf(sim, 1) == 1);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: referee front boundary commits before returning", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  SimulationAccess::RulingsOf(sim).Submit(StopPlayRuling{});
  SimulationAccess::StartPlay(sim);
  SimulationAccess::ProcessReferee(sim);
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: touch boundary commits synchronously even while stopped", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::RulingsOf(sim).Submit(StopPlayRuling{});
  AcceptedTouch touch;
  touch.player = 0;
  touch.team = TeamSide::Home;
  SimulationAccess::EventsOf(sim).OnAcceptedTouch(touch);
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: situation reset does not erase pending verdicts", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  SimulationAccess::RulingsOf(sim).Submit(AwardGoalRuling{TeamSide::Home});
  sim.ResetSituation(Vector3(0));
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 1);
}

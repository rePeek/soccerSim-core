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
// Construct controlled rule facts without running a match to reach open play.
class OpenReferee final : public Referee {
 public:
  OpenReferee() : Referee(0, Vector3(0)) { buffer.active = false; buffer.endPhase = false; }
  void FlagOffside(Player* player) { offsidePlayers = {player}; }
};
OpenReferee& Open(Simulation& sim) {
  auto rules = std::make_unique<OpenReferee>();
  auto& result = *rules;
  SimulationAccess::ReplaceRules(sim, std::move(rules));
  SimulationAccess::SetPhase(sim, MatchPhase::FirstHalf);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::StartBallInPlay(sim);
  sim.AdvanceTime(TickSpan{100});
  return result;
}
Player* Actor(Simulation& sim, int team, int slot = 1) {
  return SimulationAccess::RosterOf(sim, team).Players()[slot];
}
void Assess(Simulation& sim, Referee& rules, float severity, Vector3 position = Vector3(0)) {
  FoulAssessment fact;
  fact.kind = FoulKind::SlidingTackle;
  fact.victim = Actor(sim, 0)->GetID();
  fact.offender = Actor(sim, 1)->GetID();
  fact.victim_team_id = 0;
  fact.offender_team_id = 1;
  fact.contacted_at = SimulationAccess::NowOf(sim);
  fact.position = position;
  fact.offender_action_type = static_cast<int>(e_FunctionType_Sliding);
  fact.severity = severity;
  rules.AssessFoul(Actor(sim, 0), Actor(sim, 1), fact, fact.contacted_at);
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

TEST_CASE("Rulings: foul stop submits without reentering runtime", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f);
  const auto tick = SimulationAccess::RefereeFactsOf(sim);
  rules.CheckPendingFoul({tick}, SimulationAccess::CommandsOf(sim), SimulationAccess::RulingsOf(sim));
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim));
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 1);
  SimulationAccess::CommitRulings(sim);
  REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(sim));
  rules.CheckPendingFoul({tick}, SimulationAccess::CommandsOf(sim), SimulationAccess::RulingsOf(sim));
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: front foul stop restricts actions before player simulation", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f);
  SimulationAccess::ProcessReferee(sim);
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE_FALSE(SimulationAccess::MayTouchBallOf(sim, *Actor(sim, 0)));
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: real offside touch commits before a later touch", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  rules.FlagOffside(Actor(sim, 0));
  AcceptedTouch touch;
  touch.touched_at = SimulationAccess::NowOf(sim);
  touch.player = Actor(sim, 0)->GetID();
  touch.team = TeamSide::Home;
  SimulationAccess::EventsOf(sim).OnAcceptedTouch(touch);
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE(rules.GetBuffer().desiredSetPiece == e_GameMode_FreeKick);
  REQUIRE(rules.GetBuffer().teamID == 1);
  REQUIRE_FALSE(SimulationAccess::MayTouchBallOf(sim, *Actor(sim, 1)));
  SimulationAccess::EventsOf(sim).OnAcceptedTouch(touch);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

TEST_CASE("Rulings: touchline stop commits at the front boundary", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  SimulationAccess::ResetBall(sim, Vector3(0, 40, 0));
  SimulationAccess::ProcessReferee(sim);
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE(rules.GetBuffer().desiredSetPiece == e_GameMode_ThrowIn);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
}

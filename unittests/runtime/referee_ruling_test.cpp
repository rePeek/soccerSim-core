#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>

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
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 2);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).empty());
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

TEST_CASE("Rulings: foul awards confirm free kick or penalty only at commit", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  e_GameMode kind = e_GameMode_FreeKick;
  Vector3 foul_position(4, 3, 0);
  SECTION("free kick") {}
  SECTION("penalty") { kind = e_GameMode_Penalty; foul_position = Vector3(45, 0, 0); }
  Assess(sim, rules, 1.2f, foul_position);
  SimulationAccess::ProcessReferee(sim);
  const auto& events = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(events.size() == 1);
  const auto& award = std::get<RestartAwardedEvent>(events.front());
  REQUIRE(award.kind == kind);
  REQUIRE(award.team == TeamSide::Home);
  REQUIRE(award.tick == SimulationAccess::NowOf(sim));
  REQUIRE(award.position == rules.GetBuffer().restartPos);
}

TEST_CASE("Rulings: out of play awards distinguish corner goal kick and throw in", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  e_GameMode kind = e_GameMode_ThrowIn;
  Vector3 position(5, 40, 0);
  SECTION("throw in") {}
  SECTION("corner") { kind = e_GameMode_Corner; position = Vector3(-55, 40, 0); }
  SECTION("goal kick") { kind = e_GameMode_GoalKick; position = Vector3(55, 40, 0); }
  // Home is last to touch. At its own goal line this is a corner; at the
  // opposite goal line it is a goal kick, even when also beyond the touchline.
  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  SimulationAccess::ResetBall(sim, position);
  SimulationAccess::ProcessReferee(sim);
  const auto& events = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(events.size() == 1);
  const auto& award = std::get<RestartAwardedEvent>(events.front());
  REQUIRE(award.kind == kind);
  REQUIRE(award.team == TeamSide::Away);
  REQUIRE(award.position == rules.GetBuffer().restartPos);
  if (kind == e_GameMode_GoalKick) REQUIRE(award.position == Vector3(47, 0, 0));
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 1);
}

TEST_CASE("Rulings: ceremonial kickoffs publish one award without claiming taken", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  SECTION("opening") {}
  SECTION("second half") {
    SimulationAccess::SetPhase(sim, MatchPhase::SecondHalf);
    SimulationAccess::RulesOf(sim).OnPeriodEnded(MatchPhase::FirstHalf, Tick{}, Vector3(0), 1);
    sim.AdvanceTime(TickSpan{10});
  }
  SimulationAccess::ProcessReferee(sim);
  const auto& events = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(events.size() == 1);
  REQUIRE(std::get<RestartAwardedEvent>(events.front()).kind == e_GameMode_KickOff);
  REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(sim));
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(events.size() == 1);
}

TEST_CASE("Rulings: confirmed goal beats out of play and awards the opponent kickoff", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  // In the common rules frame Away defends the positive goal.
  SimulationAccess::MirrorTeam(sim, 1);
  const auto tick = SimulationAccess::RefereeFactsOf(sim);
  rules.GoalMouthCrossed(1, tick, &SimulationAccess::RulingsOf(sim));
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 0);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 1);
  SimulationAccess::ResetBall(sim, Vector3(55, 40, 0));
  SimulationAccess::ProcessReferee(sim);
  const auto& award = std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).back());
  REQUIRE(award.kind == e_GameMode_KickOff);
  REQUIRE(award.team == TeamSide::Away);
  REQUIRE(award.position == Vector3(0));
}

TEST_CASE("Rulings: disciplinary counts commit now and send off at the original deadline", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  Player& offender = *Actor(sim, 1);
  auto& sink = SimulationAccess::RulingsOf(sim);
  bool red = false;
  bool second_yellow = false;
  SECTION("first yellow") {}
  SECTION("second yellow") {
    second_yellow = true;
    sink.Submit(CardRuling{offender.GetID(), 2, SimulationAccess::NowOf(sim)});
    SimulationAccess::CommitRulings(sim);
  }
  SECTION("direct red") { red = true; }
  const int before = offender.CardCount();
  const auto event_start = SimulationAccess::MatchEventsOf(sim).size();
  const auto now = SimulationAccess::NowOf(sim);
  Assess(sim, rules, red ? 2.2f : 1.6f);
  const auto tick = SimulationAccess::RefereeFactsOf(sim);
  rules.CheckPendingFoul({tick}, SimulationAccess::CommandsOf(sim), sink);
  REQUIRE(offender.CardCount() == before);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == event_start);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 3);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(offender.CardCount() == before + (red ? 3 : 1));
  REQUIRE(offender.CardEffectiveTick() == now + TickSpan{600});
  REQUIRE(offender.IsActive());
  const auto& events = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(events.size() == event_start + 2);
  REQUIRE(std::holds_alternative<RestartAwardedEvent>(events[event_start]));
  const auto& card = std::get<CardShownEvent>(events.back());
  REQUIRE(card.tick == now);
  REQUIRE(card.player == offender.GetID());
  REQUIRE(card.team == TeamSide::Away);
  REQUIRE(card.red == (red || second_yellow));
  REQUIRE(card.second_yellow == second_yellow);
  // Both ordinary deadlines retain the old 10-second card-administration addition.
  REQUIRE(rules.GetBuffer().restart->earliest_restart_tick == now + TickSpan{1000});
  REQUIRE(rules.GetBuffer().restart->timeout_tick == now + TickSpan{4000});
  rules.CheckPendingFoul({tick}, SimulationAccess::CommandsOf(sim), sink);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 0);
  sim.AdvanceTime(TickSpan{599});
  offender.CommitTick(SimulationAccess::PlayerTickOf(sim, offender), 0);
  REQUIRE(offender.IsActive());
  sim.AdvanceTime(TickSpan{1});
  offender.CommitTick(SimulationAccess::PlayerTickOf(sim, offender), 0);
  REQUIRE(offender.IsActive() == (!red && !second_yellow));
}

TEST_CASE("Rulings: repeated card publication is idempotent across commit boundaries", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  Open(sim);
  Player& offender = *Actor(sim, 1);
  auto& sink = SimulationAccess::RulingsOf(sim);
  const CardRuling card{offender.GetID(), 2, SimulationAccess::NowOf(sim) + TickSpan{600}};
  sink.Submit(card);
  sink.Submit(card);
  SimulationAccess::CommitRulings(sim);
  sink.Submit(card);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(offender.CardCount() == 1);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 1);
  REQUIRE(sim.Stop());
  Init(sim);
  SimulationAccess::RulingsOf(sim).Submit(card);
  SimulationAccess::CommitRulings(sim);
  REQUIRE(Actor(sim, 1)->CardCount() == 1);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 1);
}

TEST_CASE("Rulings: invalid associated verdict rejects the whole logical batch", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  Open(sim);
  auto& sink = SimulationAccess::RulingsOf(sim);
  RefereeRuling invalid = CardRuling{football::player::kInvalidPlayerId, 2, Tick{700}};
  SECTION("unknown player") {}
  SECTION("unknown card type") { invalid = CardRuling{3, 0, Tick{700}}; }
  SECTION("past deadline") { invalid = CardRuling{3, 2, Tick{99}}; }
  SECTION("inactive player") { SimulationAccess::Deactivate(sim, *Actor(sim, 1)); invalid = CardRuling{3, 2, Tick{700}}; }
  SECTION("unsupported restart") { invalid = AwardRestartRuling{}; }
  SECTION("nonfinite restart") {
    invalid = AwardRestartRuling{e_GameMode_FreeKick, TeamSide::Home,
        Vector3(std::numeric_limits<float>::quiet_NaN(), 0, 0)};
  }
  SECTION("invalid team") { invalid = AwardGoalRuling{static_cast<TeamSide>(9)}; }
  sink.Submit(StopPlayRuling{});
  sink.Submit(AwardRestartRuling{e_GameMode_FreeKick, TeamSide::Home, Vector3(0)});
  sink.Submit(invalid);
  REQUIRE_THROWS_AS(SimulationAccess::CommitRulings(sim), std::logic_error);
  REQUIRE(SimulationAccess::IsInPlayOf(sim));
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim));
  REQUIRE(SimulationAccess::MatchEventsOf(sim).empty());
  REQUIRE(Actor(sim, 1)->CardCount() == 0);
  REQUIRE(SimulationAccess::PendingRulingsOf(sim) == 3);
}

TEST_CASE("Rulings: a physical goal commits score scorer and event in the same step", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  Open(sim);
  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  auto state = SimulationAccess::BallOf(sim)->state();
  state.position = Vector3(52.45f, 0, 0.6f);
  state.velocity = Vector3(40, 0, 0); // Cross the whole-ball goal plane this step.
  SimulationAccess::BallOf(sim)->Reset(state);
  sim.Step(PlayerControlSet{});
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 1);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 1);
  const auto goal = std::get<GoalScoredEvent>(SimulationAccess::MatchEventsOf(sim).front());
  REQUIRE(goal.tick == Tick{101});
  REQUIRE(goal.scorer == 1);
  REQUIRE_FALSE(goal.own_goal);
  REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(sim));
  // Duplicate goal reports cannot increment again or duplicate the formal event.
  SimulationAccess::RulingsOf(sim).Submit(AwardGoalRuling{TeamSide::Home});
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 1);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 1);
  sim.Step(PlayerControlSet{});
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == 2);
  REQUIRE(std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).back()).kind == e_GameMode_KickOff);
}

TEST_CASE("Rulings: standing foul advantage retains its recall and expiry timing", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  FoulAssessment fact;
  fact.kind = FoulKind::StandingFall;
  fact.victim_team_id = 0;
  fact.offender_team_id = 1;
  fact.victim_team_fading_possession = 1.2f;
  fact.offender_action_type = static_cast<int>(e_FunctionType_Interfere);
  const auto now = SimulationAccess::NowOf(sim);
  rules.AssessFoul(Actor(sim, 0), Actor(sim, 1), fact, now);
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim));
  REQUIRE(SimulationAccess::MatchEventsOf(sim).empty());
  SimulationAccess::TeamStateOf(sim, 0).fading_possession_amount = 0.5f;
  Actor(sim, 0)->SetTeamContext(SimulationAccess::TeamFactsOf(sim, 0));
  bool expires = false;
  SECTION("failed advantage recalled") { sim.AdvanceTime(TickSpan{61}); }
  SECTION("successful advantage expires") { expires = true; sim.AdvanceTime(TickSpan{301}); }
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim) == expires);
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == (expires ? 0 : 1));
  if (!expires) {
    const auto& award = std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).front());
    REQUIRE(award.kind == e_GameMode_FreeKick);
    REQUIRE(award.team == TeamSide::Home);
  }
}

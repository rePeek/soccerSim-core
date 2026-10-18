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
// Three outfield identities so a declared order can differ from the nearest.
TeamSetup BigTeam(football::player::PlayerId first) {
  TeamSetup team;
  for (int i = 0; i < 3; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + i;
    team.players.push_back(player);
  }
  team.tactical_formation = {{{-1, 0}, e_PlayerRole_GK},
                             {{0.3f, 0.2f}, e_PlayerRole_CF},
                             {{0.7f, -0.2f}, e_PlayerRole_CF}};
  return team;
}
void InitWith(Simulation& sim, const TeamSetup& home, const TeamSetup& away) {
  MatchOptions options;
  options.snapshot_capacity = 8;
  sim.Init(home, away, football::pitch::Pitch{}, options, football::ball::BallModel{});
}
// Construct controlled rule facts without running a match to reach open play.
class OpenReferee final : public Referee {
 public:
  OpenReferee() : Referee() {}
  void FlagOffside(Player* player) { offsidePlayers = {player}; }
};
OpenReferee& Open(Simulation& sim) {
  auto rules = std::make_unique<OpenReferee>();
  auto& result = *rules;
  SimulationAccess::ReplaceRules(sim, std::move(rules));
  SimulationAccess::ClearRestart(sim);
  SimulationAccess::SetPhase(sim, MatchPhase::FirstHalf);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::StartBallInPlay(sim);
  sim.AdvanceTime(TickSpan{100});
  return result;
}
Player* Actor(Simulation& sim, int team, int slot = 1) {
  return SimulationAccess::TeamOf(sim, team).Players()[slot];
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
  REQUIRE(SimulationAccess::RestartContextOf(sim).kind == e_GameMode_FreeKick);
  REQUIRE(SimulationAccess::RestartContextOf(sim).team_id == 1);
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
  REQUIRE(SimulationAccess::RestartContextOf(sim).kind == e_GameMode_ThrowIn);
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
  REQUIRE(events.size() == 2);  // RestartAwarded + FoulConfirmed
  const auto& award = std::get<RestartAwardedEvent>(events.front().event);
  REQUIRE(award.kind == kind);
  REQUIRE(award.team == TeamSide::Home);
  REQUIRE(award.tick == SimulationAccess::NowOf(sim));
  REQUIRE(award.position == SimulationAccess::RestartContextOf(sim).position);
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
  const auto& award = std::get<RestartAwardedEvent>(events.front().event);
  REQUIRE(award.kind == kind);
  REQUIRE(award.team == TeamSide::Away);
  REQUIRE(award.position == SimulationAccess::RestartContextOf(sim).position);
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
  REQUIRE(std::get<RestartAwardedEvent>(events.front().event).kind == e_GameMode_KickOff);
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
  const auto& award = std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).back().event);
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
  REQUIRE(events.size() == event_start + 3);  // Awarded + FoulConfirmed + Card
  REQUIRE(std::holds_alternative<RestartAwardedEvent>(events[event_start].event));
  const auto& card = std::get<CardShownEvent>(events.back().event);
  REQUIRE(card.tick == now);
  REQUIRE(card.player == offender.GetID());
  REQUIRE(card.team == TeamSide::Away);
  REQUIRE(card.red == (red || second_yellow));
  REQUIRE(card.second_yellow == second_yellow);
  // Both ordinary deadlines retain the old 10-second card-administration addition.
  REQUIRE(SimulationAccess::RestartContextOf(sim).state->earliest_restart_tick == now + TickSpan{1000});
  REQUIRE(SimulationAccess::RestartContextOf(sim).state->timeout_tick == now + TickSpan{4000});
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
  const auto goal = std::get<GoalScoredEvent>(SimulationAccess::MatchEventsOf(sim).front().event);
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
  REQUIRE(std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).back().event).kind == e_GameMode_KickOff);
}

TEST_CASE("Rulings: goal lookahead without net damping preserves boundary verdicts",
          "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  Open(sim);
  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  auto* ball = SimulationAccess::BallOf(sim);
  auto state = ball->state();
  state.position = Vector3(52.45f, 0.0f, 0.6f);
  state.velocity = Vector3(40.0f, 0.0f, 0.0f);
  bool expect_goal = true;
  bool check_duplicate = false;
  SECTION("central goal") {}
  SECTION("outside the post is a goal kick") {
    state.position.coords[1] = 5.0f;
    expect_goal = false;
  }
  SECTION("above the crossbar is a goal kick") {
    state.position.coords[2] = 3.5f;
    expect_goal = false;
  }
  SECTION("continued flight after a goal cannot score twice") {
    check_duplicate = true;
  }
  ball->Reset(state);
  // Exercise StepImpl's actual prediction gate and segment classification.
  sim.Step(PlayerControlSet{});
  REQUIRE(ball->state().position.coords[0] > 52.5f);
  REQUIRE(ball->state().velocity.coords[0] > 0.0f);
  REQUIRE(SimulationAccess::ScoreOf(sim, 0) == (expect_goal ? 1 : 0));
  REQUIRE(SimulationAccess::ScoreOf(sim, 1) == 0);
  const auto goal_count = [&] {
    const auto& events = SimulationAccess::MatchEventsOf(sim);
    return std::count_if(events.begin(), events.end(), [](const auto& event) {
      return std::holds_alternative<GoalScoredEvent>(event.event);
    });
  };
  REQUIRE(goal_count() == (expect_goal ? 1 : 0));
  if (expect_goal) REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(sim));
  // The next front boundary must choose kickoff for a goal, otherwise goal kick.
  sim.Step(PlayerControlSet{});
  REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(sim));
  REQUIRE(SimulationAccess::RestartContextOf(sim).kind ==
          (expect_goal ? e_GameMode_KickOff : e_GameMode_GoalKick));
  REQUIRE(SimulationAccess::RestartContextOf(sim).team_id == 1);
  REQUIRE(goal_count() == (expect_goal ? 1 : 0));
  if (check_duplicate) {
    // Feed continued motion beyond the former rear net through the real Step
    // path while the kickoff is pending, rather than submitting a fake goal.
    state.position = Vector3(56.0f, 0.0f, 0.6f);
    ball->Reset(state);
    sim.Step(PlayerControlSet{});
    // Restart preparation restores the ball to the centre, not to a net.
    REQUIRE(ball->state().position == Vector3(0, 0, ball->config().radius()));
    REQUIRE(SimulationAccess::ScoreOf(sim, 0) == 1);
    REQUIRE(goal_count() == 1);
    REQUIRE(SimulationAccess::RestartContextOf(sim).kind == e_GameMode_KickOff);
  }
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
  REQUIRE(SimulationAccess::MatchEventsOf(sim).size() == (expires ? 0 : 2));
  if (!expires) {
    const auto& award = std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).front().event);
    REQUIRE(award.kind == e_GameMode_FreeKick);
    REQUIRE(award.team == TeamSide::Home);
  }
}

TEST_CASE("Rulings: runtime restart context is what players and world read", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f);
  SimulationAccess::ProcessReferee(sim);  // stop + award committed synchronously
  const football::sim::RestartContext& restart = SimulationAccess::RestartContextOf(sim);
  REQUIRE(restart.active);
  REQUIRE(restart.kind == e_GameMode_FreeKick);
  REQUIRE(restart.team_id == 0);
  REQUIRE(restart.state.has_value());
  REQUIRE(restart.state->phase == football::sim::RestartPhase::Pending);
  // The per-player execution view borrows the same Runtime-owned context.
  REQUIRE(&SimulationAccess::PlayerTickOf(sim, *Actor(sim, 0)).restart == &restart);

  const WorldState world = sim.Observe();
  REQUIRE(world.restart == e_GameMode_FreeKick);
  REQUIRE(world.restart_pending);

  // The next instant plans the restart; the taker and targets stay Runtime-owned.
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(restart.taker != nullptr);
  REQUIRE(restart.state->plan.taker == restart.taker);
  REQUIRE(restart.taker->IsActive());
  const WorldState planned = sim.Observe();
  REQUIRE(planned.restart_taker.has_value());
  REQUIRE(*planned.restart_taker == restart.taker->GetID());
}

TEST_CASE("Rulings: opening ceremony state is runtime-owned", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  const football::sim::RestartContext& restart = SimulationAccess::RestartContextOf(sim);
  REQUIRE(restart.active);
  REQUIRE(restart.kind == e_GameMode_KickOff);
  REQUIRE(restart.taker == nullptr);
  REQUIRE_FALSE(restart.state.has_value());

  SimulationAccess::ProcessReferee(sim);  // placement + confirmed award
  REQUIRE(restart.taker != nullptr);
  REQUIRE_FALSE(restart.state.has_value());
  REQUIRE(std::get<RestartAwardedEvent>(SimulationAccess::MatchEventsOf(sim).front().event).kind ==
          e_GameMode_KickOff);
}

TEST_CASE("Rulings: play state derives from runtime authorization and restart", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  // Opening: play is stopped and no restart has been organized yet.
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::Stopped);

  auto& rules = Open(sim);
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::Playing);

  Assess(sim, rules, 1.2f);
  SimulationAccess::ProcessReferee(sim);  // committed stop + restart award
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::RestartSetup);

  // The setup instant plans the restart; the harness never steps physics, so
  // drive the documented timeout fallback instead. It repairs positions and
  // must not invent a touch or a decision.
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::RestartContextOf(sim).taker != nullptr);
  sim.AdvanceTime(TickSpan{3001});
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::RestartReady);

  // A confirmed legal release is Taken until the context completes.
  SimulationAccess::RulingsOf(sim).Submit(BallInPlayRuling{});
  SimulationAccess::CommitRulings(sim);
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::RestartReady);
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::Playing);
  REQUIRE_FALSE(SimulationAccess::RestartContextOf(sim).active);
}

TEST_CASE("Rulings: team free-kick order overrides the nearest taker",
          "[runtime][ruling][team]") {
  // The declared order names the goalkeeper, deliberately the farthest actor
  // from the foul spot.
  TeamSetup ordered_home = BigTeam(0);
  ordered_home.set_piece_orders.direct_free_kicks = {0};
  Simulation ordered;
  InitWith(ordered, ordered_home, Team(100));
  auto& ordered_rules = Open(ordered);
  Assess(ordered, ordered_rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(ordered);
  SimulationAccess::ProcessReferee(ordered);
  const RestartContext& ordered_restart = SimulationAccess::RestartContextOf(ordered);
  REQUIRE(ordered_restart.state.has_value());
  REQUIRE(ordered_restart.kind == e_GameMode_FreeKick);
  REQUIRE(ordered_restart.state->plan.taker != nullptr);
  REQUIRE(ordered_restart.state->plan.taker->GetID() == 0);

  // No declared order: the nearest active candidate takes it (legacy ranking).
  Simulation legacy;
  InitWith(legacy, BigTeam(0), Team(100));
  auto& legacy_rules = Open(legacy);
  Assess(legacy, legacy_rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(legacy);
  SimulationAccess::ProcessReferee(legacy);
  const RestartContext& legacy_restart = SimulationAccess::RestartContextOf(legacy);
  REQUIRE(legacy_restart.state->plan.taker != nullptr);
  REQUIRE(legacy_restart.state->plan.taker->GetID() != 0);
}

TEST_CASE("Rulings: team formation presets seed both sides' restart targets",
          "[runtime][ruling][team]") {
  const football::team::FormationAnchor home_anchor{
      football::team::AnchorFrame::TeamRelative, 0.9f, 0.0f};
  // Distinct anchors so the two slot-1 presets cannot collide and force the
  // deterministic separation search to move one of them.
  const football::team::FormationAnchor away_anchor{
      football::team::AnchorFrame::TeamRelative, 0.2f, 0.0f};
  TeamSetup home = BigTeam(0);
  home.set_piece_orders.direct_free_kicks = {0};
  home.formation_presets.presets = {{"fk_attack",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Attacking,
      {{1, home_anchor, "runner"}}}};
  TeamSetup away = Team(100);
  away.formation_presets.presets = {{"fk_defend",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Defending,
      {{1, away_anchor, "wall"}}}};
  Simulation sim;
  InitWith(sim, home, away);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(sim);
  SimulationAccess::ProcessReferee(sim);
  const RestartContext& restart = SimulationAccess::RestartContextOf(sim);
  REQUIRE(restart.state.has_value());
  const RestartPlan& plan = restart.state->plan;

  const auto target_of = [&](football::player::PlayerId id) {
    for (const RestartPlayerTarget& target : plan.players) {
      if (target.player->GetID() == id) return target.position;
    }
    return Vector3(-999, -999, -999);
  };
  // The home slot 1 identity and the away slot 1 identity follow their presets;
  // legalization for this free kick leaves both home-frame candidates intact.
  const football::pitch::Pitch& pitch = SimulationAccess::PitchOf(sim);
  REQUIRE(target_of(1) == football::team::FormationAnchorToWorld(
                              home_anchor, pitch, -1, plan.ball_position, false));
  REQUIRE(target_of(101) == football::team::FormationAnchorToWorld(
                               away_anchor, pitch, 1, plan.ball_position, false));
  // The declared taker still owns the ball spot, not a preset position.
  REQUIRE(plan.taker != nullptr);
  REQUIRE(plan.taker->GetID() == 0);
}

namespace {
Vector3 PlanTargetOf(const RestartPlan& plan, football::player::PlayerId id) {
  for (const RestartPlayerTarget& target : plan.players) {
    if (target.player->GetID() == id) return target.position;
  }
  return Vector3(-999, -999, -999);
}

const RestartPlan& PlanOf(Simulation& sim) {
  const RestartContext& restart = SimulationAccess::RestartContextOf(sim);
  REQUIRE(restart.state.has_value());
  return restart.state->plan;
}
}  // namespace

TEST_CASE("Rulings: preset targets keep the team-relative frame after a change of ends",
          "[runtime][ruling][team]") {
  const football::team::FormationAnchor anchor{
      football::team::AnchorFrame::TeamRelative, 0.9f, 0.2f};
  TeamSetup home = BigTeam(0);
  home.formation_presets.presets = {{"fk_attack",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Attacking,
      {{1, anchor, "runner"}}}};
  Simulation sim;
  InitWith(sim, home, Team(100));
  auto& rules = Open(sim);
  // Half-time change of ends: both runtime directions flip, but the fixed
  // home-frame policy representation (and therefore TeamRelative anchors) must
  // not follow the stadium orientation.
  SimulationAccess::SwitchTeamEnds(sim, 0);
  SimulationAccess::SwitchTeamEnds(sim, 1);
  sim.Mirror(false, false, true);
  Assess(sim, rules, 1.2f, Vector3(40, 20, 0));
  SimulationAccess::ProcessReferee(sim);
  SimulationAccess::ProcessReferee(sim);
  const RestartPlan& plan = PlanOf(sim);
  const auto& pitch = SimulationAccess::PitchOf(sim);
  REQUIRE(PlanTargetOf(plan, 1) ==
          football::team::FormationAnchorToWorld(anchor, pitch, -1,
                                                  plan.ball_position, false));
}

TEST_CASE("Rulings: a sent-off preset slot is vacated and skipped",
          "[runtime][ruling][team]") {
  const football::team::FormationAnchor slot1{
      football::team::AnchorFrame::TeamRelative, 0.9f, 0.0f};
  const football::team::FormationAnchor slot2{
      football::team::AnchorFrame::TeamRelative, 0.7f, 0.0f};
  TeamSetup home = BigTeam(0);
  // Declare the goalkeeper as taker so slot 1 is free to follow its preset.
  home.set_piece_orders.direct_free_kicks = {0};
  home.formation_presets.presets = {{"fk_attack",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Attacking,
      {{1, slot1, "a"}, {2, slot2, "b"}}}};
  Simulation sim;
  InitWith(sim, home, Team(100));
  auto& rules = Open(sim);
  Player* sent_off = SimulationAccess::TeamOf(sim, 0).Players()[2];
  SimulationAccess::SendOff(sim, *sent_off);
  REQUIRE_FALSE(
      SimulationAccess::ActiveLineupOf(sim, 0).slots[2].has_value());
  Assess(sim, rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(sim);
  SimulationAccess::ProcessReferee(sim);
  const RestartPlan& plan = PlanOf(sim);
  // The dismissed actor is not planned; the surviving preset slot still is.
  for (const RestartPlayerTarget& target : plan.players)
    CHECK(target.player->GetID() != sent_off->GetID());
  const auto& pitch = SimulationAccess::PitchOf(sim);
  REQUIRE(PlanTargetOf(plan, 1) ==
          football::team::FormationAnchorToWorld(slot1, pitch, -1,
                                                  plan.ball_position, false));
}

TEST_CASE("Rulings: an illegal preset proposal is corrected by the legality pipeline",
          "[runtime][ruling][team]") {
  // The defending preset proposes the ball centre, which the free-kick
  // exclusion distance must push out. The raw candidate is illegal.
  const football::team::FormationAnchor on_ball{
      football::team::AnchorFrame::BallRelative, 0.0f, 0.0f};
  TeamSetup home = BigTeam(0);
  TeamSetup away = BigTeam(100);
  away.formation_presets.presets = {{"fk_defend",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Defending,
      {{1, on_ball, "wall"}}}};
  Simulation sim;
  InitWith(sim, home, away);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(sim);
  SimulationAccess::ProcessReferee(sim);
  const RestartPlan& plan = PlanOf(sim);
  const auto& pitch = SimulationAccess::PitchOf(sim);
  const Vector3 raw = football::team::FormationAnchorToWorld(
      on_ball, pitch, 1, plan.ball_position, false);
  const Vector3 corrected = PlanTargetOf(plan, 101);
  CHECK(corrected != raw);
  CHECK((corrected - plan.ball_position).GetLength() >= 9.15f);
}

TEST_CASE("Rulings: a partial preset only moves its declared slots",
          "[runtime][ruling][team]") {
  const football::team::FormationAnchor anchor{
      football::team::AnchorFrame::TeamRelative, 0.95f, 0.15f};
  TeamSetup with_preset = BigTeam(0);
  // Keep the taker on slot 0 so the preset slot is not shadowed by the taker.
  with_preset.set_piece_orders.direct_free_kicks = {0};
  with_preset.formation_presets.presets = {{"fk_attack",
      football::team::FormationPresetKind::FreeKick,
      football::team::FormationPresetSide::Attacking,
      {{1, anchor, "runner"}}}};
  TeamSetup plain = BigTeam(0);
  plain.set_piece_orders.direct_free_kicks = {0};
  Simulation preset_sim;
  Simulation plain_sim;
  InitWith(preset_sim, with_preset, Team(100));
  InitWith(plain_sim, plain, Team(100));
  auto& preset_rules = Open(preset_sim);
  auto& plain_rules = Open(plain_sim);
  Assess(preset_sim, preset_rules, 1.2f, Vector3(4, 3, 0));
  Assess(plain_sim, plain_rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(preset_sim);
  SimulationAccess::ProcessReferee(preset_sim);
  SimulationAccess::ProcessReferee(plain_sim);
  SimulationAccess::ProcessReferee(plain_sim);
  const RestartPlan& preset_plan = PlanOf(preset_sim);
  const RestartPlan& plain_plan = PlanOf(plain_sim);
  // Slot 1 follows its preset; every undeclared slot keeps the legacy target
  // and the taker selection is unaffected by preset data.
  REQUIRE(preset_plan.taker->GetID() == plain_plan.taker->GetID());
  CHECK(PlanTargetOf(preset_plan, 1) != PlanTargetOf(plain_plan, 1));
  CHECK(PlanTargetOf(preset_plan, 2) == PlanTargetOf(plain_plan, 2));
  CHECK(PlanTargetOf(preset_plan, 0) == PlanTargetOf(plain_plan, 0));
}

TEST_CASE("Rulings: foul publishes a distinct award and confirmation", "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  Assess(sim, rules, 1.2f, Vector3(4, 3, 0));
  SimulationAccess::ProcessReferee(sim);
  const auto& records = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(records.size() == 2);
  REQUIRE(std::holds_alternative<RestartAwardedEvent>(records[0].event));
  REQUIRE(std::holds_alternative<FoulConfirmedEvent>(records[1].event));
  const auto& award = std::get<RestartAwardedEvent>(records[0].event);
  REQUIRE(award.kind == e_GameMode_FreeKick);
  REQUIRE(award.restart_id != 0);
  const auto& foul = std::get<FoulConfirmedEvent>(records[1].event);
  REQUIRE(foul.team == TeamSide::Away);
  REQUIRE(foul.offender == Actor(sim, 1)->GetID());
  REQUIRE(foul.victim == Actor(sim, 0)->GetID());

  // Setup, the documented timeout fallback, then a legal release.
  SimulationAccess::ProcessReferee(sim);
  sim.AdvanceTime(TickSpan{3001});
  SimulationAccess::ProcessReferee(sim);
  REQUIRE(SimulationAccess::PlayStateOf(sim) == football::sim::PlayState::RestartReady);
  SimulationAccess::RulingsOf(sim).Submit(BallInPlayRuling{});
  SimulationAccess::CommitRulings(sim);
  const auto& after = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(std::holds_alternative<RestartTakenEvent>(after.back().event));
  const auto& taken = std::get<RestartTakenEvent>(after.back().event);
  REQUIRE(taken.kind == e_GameMode_FreeKick);
  REQUIRE(taken.restart_id == award.restart_id);
  REQUIRE(taken.taker != football::player::kInvalidPlayerId);
}

TEST_CASE("Rulings: offside publishes a confirmation distinct from the award",
          "[runtime][ruling]") {
  Simulation sim;
  Init(sim);
  auto& rules = Open(sim);
  rules.FlagOffside(Actor(sim, 0));
  AcceptedTouch touch;
  touch.touched_at = SimulationAccess::NowOf(sim);
  touch.player = Actor(sim, 0)->GetID();
  touch.team = TeamSide::Home;
  SimulationAccess::EventsOf(sim).OnAcceptedTouch(touch);
  const auto& records = SimulationAccess::MatchEventsOf(sim);
  REQUIRE(records.size() == 2);
  REQUIRE(std::holds_alternative<RestartAwardedEvent>(records[0].event));
  REQUIRE(std::holds_alternative<OffsideConfirmedEvent>(records[1].event));
  const auto& offside = std::get<OffsideConfirmedEvent>(records[1].event);
  REQUIRE(offside.team == TeamSide::Home);
  REQUIRE(offside.player == Actor(sim, 0)->GetID());
}

TEST_CASE("Rulings: period end publishes exactly one formal event", "[runtime][ruling]") {
  Simulation sim;
  MatchOptions options;
  options.snapshot_capacity = 8;
  options.half_duration = Seconds(1);
  sim.Init(Team(0), Team(2), football::pitch::Pitch{}, options, football::ball::BallModel{});
  Open(sim);
  REQUIRE(SimulationAccess::PhaseOf(sim) == MatchPhase::FirstHalf);
  sim.Step(PlayerControlSet{});
  int first_half_ends = 0;
  for (const auto& record : SimulationAccess::MatchEventsOf(sim)) {
    const auto* ended = std::get_if<PeriodEndedEvent>(&record.event);
    if (ended != nullptr && ended->phase == MatchPhase::FirstHalf) ++first_half_ends;
  }
  REQUIRE(first_half_ends == 1);
  REQUIRE(SimulationAccess::PhaseOf(sim) == MatchPhase::SecondHalf);
}

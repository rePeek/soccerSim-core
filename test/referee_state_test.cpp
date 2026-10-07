#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
#include "sim/simulation.hpp"
#include "sim/rules/ball_touch_facts.hpp"
#include "sim/testing/simulation_access.hpp"
#include "rule_command_fixture.hpp"
#include "sim/event/ball_touch_dispatcher.hpp"
using football::sim::testing::SimulationAccess;

namespace {
using namespace football::sim;
using blunted::Vector3;

struct RefereeStateFixture : Referee {
  explicit RefereeStateFixture(Match* match)
      : Referee(*match->GetTeam(match->FirstTeam()), match->options().ball_position) {}
  using Referee::buffer;
  using Referee::foul;
  using Referee::offsidePlayers;
  using Referee::post_restart_relax_;
};

TEST_CASE("period end updates only referee facts from explicit inputs", "[sim][referee][period]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto& kickoff_team = *match.GetTeam(1);
  auto* actor = kickoff_team.GetAllPlayers()[1];
  for (auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
    RefereeStateFixture referee(&match);
    referee.buffer.taker = actor;
    referee.buffer.restart.emplace();
    referee.buffer.restart->phase = RestartPhase::Pending;
    referee.foul.foulPlayer = actor;
    referee.foul.foulVictim = actor;
    referee.foul.foulType = 3;
    referee.foul.advantage = true;
    referee.foul.foul_tick = Tick{7};
    referee.foul.hasBeenProcessed = false;
    referee.offsidePlayers = {actor};
    referee.post_restart_relax_ = TickSpan{12};
    const auto original = referee.buffer;
    const auto rng = match.rng().engine();
    const auto before = simulation.Observe();
    referee.OnPeriodEnded(phase, Tick{123}, Vector3(3, 4, 0), kickoff_team);
    REQUIRE(referee.buffer.taker == nullptr);
    REQUIRE_FALSE(referee.buffer.restart);
    REQUIRE(referee.post_restart_relax_ == TickSpan{12});
    REQUIRE(referee.offsidePlayers == std::vector<Player*>{actor});
    REQUIRE(referee.foul.foulVictim == actor); // Preserve historically untouched facts.
    if (phase == MatchPhase::FirstHalf) {
      REQUIRE(referee.foul.foulPlayer == nullptr);
      REQUIRE(referee.foul.foulType == 0);
      REQUIRE_FALSE(referee.foul.advantage);
      REQUIRE(referee.foul.foul_tick == Tick{});
      REQUIRE(referee.foul.hasBeenProcessed);
      REQUIRE(referee.buffer.active);
      REQUIRE(referee.buffer.endPhase);
      REQUIRE(referee.buffer.desiredSetPiece == e_GameMode_KickOff);
      REQUIRE(referee.buffer.stop_tick == Tick{123});
      REQUIRE(referee.buffer.prepare_tick == Tick{133});
      REQUIRE(referee.buffer.start_tick == Tick{153});
      REQUIRE(referee.buffer.restartPos == Vector3(3, 4, 0));
      REQUIRE(referee.buffer.teamID == 1);
      REQUIRE(referee.buffer.setpiece_team == &kickoff_team);
    } else {
      REQUIRE_FALSE(referee.buffer.active);
      REQUIRE_FALSE(referee.buffer.endPhase);
      REQUIRE(referee.foul.foulPlayer == actor);
      REQUIRE(referee.foul.foulType == 3);
      REQUIRE(referee.foul.advantage);
      REQUIRE(referee.foul.foul_tick == Tick{7});
      REQUIRE_FALSE(referee.foul.hasBeenProcessed);
      REQUIRE(referee.buffer.stop_tick == original.stop_tick);
      REQUIRE(referee.buffer.prepare_tick == original.prepare_tick);
      REQUIRE(referee.buffer.start_tick == original.start_tick);
      REQUIRE(referee.buffer.teamID == original.teamID);
      REQUIRE(referee.buffer.setpiece_team == original.setpiece_team);
      REQUIRE(referee.buffer.restartPos == original.restartPos);
    }
    REQUIRE(match.rng().engine() == rng);
    const auto after = simulation.Observe();
    REQUIRE(after.tick == before.tick);
    REQUIRE(after.phase == before.phase);
    REQUIRE(after.half_underway == before.half_underway);
    REQUIRE(after.ball_in_play == before.ball_in_play);
    REQUIRE(after.reset_sequence == before.reset_sequence);
    REQUIRE(after.ball_position == before.ball_position);
    REQUIRE(after.regulation_time == before.regulation_time);
    REQUIRE(after.ball_in_play_time == before.ball_in_play_time);
    REQUIRE(match.GetTeam(0)->GetStaticSide() == -1);
    REQUIRE(match.GetTeam(1)->GetStaticSide() == 1);
  }
}

TEST_CASE("Referee invokes the explicit reset action synchronously once before restart planning",
          "[sim][referee][reset][history]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    football::test::TakeKickOff(simulation);
    for (int tick = 0; tick < 40; ++tick) simulation.Step({});
    auto& match = *simulation.match();
    auto& referee = *match.GetReferee();
    match.GetBall()->ResetSituation(Vector3(10, 40, 0));
    simulation.Step({}); // Out-of-play classification, no setup/reset yet.
    REQUIRE(referee.GetBuffer().restart);
    REQUIRE_FALSE(referee.GetBuffer().restart->setup_done);
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto sequence = match.GetResetSequence();
    const auto now = match.GetTimelineTick();
    int calls = 0;
    simulation.Mirror(reverse, !reverse, false);
    football::test::RuleCommandProbe commands(&SimulationAccess::CommandsOf(simulation));
    commands.reset = [&](const Vector3& focus) {
      REQUIRE(++calls == 1);
      REQUIRE_FALSE(referee.GetBuffer().restart->setup_done);
      REQUIRE(referee.GetBuffer().taker == nullptr);
      REQUIRE(match.GetResetSequence() == sequence);
      REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
      simulation.ResetSituation(focus);
      REQUIRE(match.GetResetSequence() == sequence + 1);
      REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
      REQUIRE(match.GetTimelineTick() == now);
    };
    referee.Process(SimulationAccess::RefereeFactsOf(simulation), match.options(), match.rng(), commands);
    REQUIRE(calls == 1);
    REQUIRE(referee.GetBuffer().restart->setup_done);
    REQUIRE(referee.GetBuffer().taker != nullptr);
    REQUIRE_THROWS_AS(simulation.GetMentalImage(TickSpan{}), std::logic_error);
    const auto rng = match.rng().engine();
    // This fresh per-call action must not be called again on the same pending restart.
    commands.reset = [](const Vector3&) { FAIL("restart reset was repeated"); };
    referee.Process(SimulationAccess::RefereeFactsOf(simulation), match.options(), match.rng(), commands);
    REQUIRE(match.GetResetSequence() == sequence + 1);
    REQUIRE(match.rng().engine() == rng);
    simulation.Mirror(reverse, !reverse, false);
    simulation.Step({});
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
  }
}

// These rule-only fixtures never execute actors with the synthetic action. Restore
// the authoritative action before teardown; do not add a production test setter.
struct ScopedRuleAction {
  PlayerActionState& state;
  PlayerActionState original;
  explicit ScopedRuleAction(Player& player)
      : state(const_cast<PlayerActionState&>(player.GetSimulationActionState())), original(state) {}
  ~ScopedRuleAction() { state = original; }
};

TEST_CASE("standing trip notices use explicit Ball and tick facts without a Match read",
          "[sim][referee][foul]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    auto& match = *simulation.match();
    auto* victim = match.GetTeam(match.FirstTeam())->GetAllPlayers()[1];
    auto* tackler = match.GetTeam(match.SecondTeam())->GetAllPlayers()[1];
    victim->ResetPosition(Vector3(3, 4, 0), Vector3(4, 4, 0));
    tackler->ResetPosition(Vector3(2, 4, 0), Vector3(4, 4, 0));
    victim->GetTeam()->SetFadingTeamPossessionAmount(1.2f);
    tackler->GetTeam()->SetFadingTeamPossessionAmount(1.2f);
    ScopedRuleAction action(*tackler);
    action.state.type = e_FunctionType_Interfere;
    const auto rng = match.rng().engine();
    const auto ball = match.GetBall()->Predict(0);
    const auto sequence = match.GetResetSequence();
    // Referee has no runtime-owner dependency; only explicit inputs and live actors.
    for (int scenario = 0; scenario < 7; ++scenario) {
      CAPTURE(reverse, scenario);
      RefereeStateFixture referee(&match);
      referee.buffer.active = false;
      Vector3 ball_position(3, 4, 100); // Standing distance retains the old 2D projection.
      if (scenario == 1) ball_position.coords[0] += 2; // Strict distance <2.
      // The legacy >1.1 uses a double literal: float 1.1f is slightly ABOVE it.
      if (scenario == 2) victim->GetTeam()->SetFadingTeamPossessionAmount(0x1.199998p0f);
      else victim->GetTeam()->SetFadingTeamPossessionAmount(scenario == 6 ? 1.1f : 1.2f);
      action.state.type = scenario == 3 ? e_FunctionType_Movement : e_FunctionType_Interfere;
      referee.buffer.active = scenario == 4;
      referee.TripNotice(scenario == 5 ? tackler : victim, tackler, 2, Tick{9001}, ball_position);
      if (scenario == 0 || scenario == 6) {
        REQUIRE(referee.foul.foulType == 1);
        REQUIRE(referee.foul.advantage);
        REQUIRE(referee.foul.foulPlayer == tackler);
        REQUIRE(referee.foul.foulVictim == victim);
        REQUIRE(referee.foul.foul_tick == Tick{9001});
        REQUIRE(referee.foul.foulPosition == victim->GetPitchPosition());
        REQUIRE_FALSE(referee.foul.hasBeenProcessed);
      } else {
        REQUIRE(referee.foul.foulType == 0);
        REQUIRE(referee.foul.foul_tick == Tick{});
        REQUIRE(referee.foul.hasBeenProcessed);
      }
    }
    REQUIRE(match.GetTimelineTick() == Tick{});
    REQUIRE(match.GetResetSequence() == sequence);
    REQUIRE(match.rng().engine() == rng);
    REQUIRE(match.GetBall()->Predict(0) == ball);
  }
}

TEST_CASE("sliding trip notices preserve strict grace, 3D radius, severity and duplicate gates",
          "[sim][referee][foul]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* victim = match.GetTeam(0)->GetAllPlayers()[1];
  auto* tackler = match.GetTeam(1)->GetAllPlayers()[1];
  victim->ResetPosition(Vector3(3, 4, 0), Vector3(4, 4, 0));
  tackler->ResetPosition(Vector3(2, 4, 0), Vector3(4, 4, 0));
  tackler->SetLastTouchTick(Tick{100});
  ScopedRuleAction action(*tackler);
  action.state.type = e_FunctionType_Sliding;
  action.state.contact.reset();
  const auto live_ball = match.GetBall()->Predict(0);
  for (int scenario = 0; scenario < 9; ++scenario) {
    CAPTURE(scenario);
    RefereeStateFixture referee(&match);
    referee.buffer.active = false;
    action.state.type = scenario == 3 ? e_FunctionType_Interfere : e_FunctionType_Sliding;
    action.state.contact.reset();
    Vector3 ball_position(3, 4, 0);
    Tick now{161}; // Last touch +60 is excluded, +61 is eligible.
    if (scenario == 1) now = Tick{160};
    if (scenario == 2) ball_position.coords[2] = 8; // Strict full-3D radius <8.
    if (scenario >= 5) {
      action.state.contact = TickSpan{10};
      action.state.elapsed = TickSpan{scenario == 5 ? 10u : 5u};
      action.state.contactPosition = Vector3(3, 4, 0);
    }
    if (scenario == 7) ball_position.coords[1] += 2;
    if (scenario == 8) victim->ResetPosition(Vector3(3, 4, 0), Vector3(2, 4, 0));
    const auto rng = match.rng().engine(); // Fixture ResetPosition itself consumes RNG.
    referee.TripNotice(scenario == 4 ? tackler : victim, tackler, 3, now, ball_position);
    const int expected = scenario == 0 || scenario == 7 ? 2 : scenario == 6 ? 1 : 0;
    REQUIRE(referee.foul.foulType == expected);
    if (expected) {
      REQUIRE_FALSE(referee.foul.advantage);
      REQUIRE_FALSE(referee.foul.hasBeenProcessed);
      REQUIRE(referee.foul.foulPlayer == tackler);
      REQUIRE(referee.foul.foulVictim == victim);
      REQUIRE(referee.foul.foul_tick == now);
      REQUIRE(referee.foul.foulPosition == victim->GetPitchPosition());
      referee.TripNotice(victim, tackler, 3, Tick{9002}, Vector3(3, 4, 0));
      REQUIRE(referee.foul.foulType == expected);
      REQUIRE(referee.foul.foul_tick == now); // Same tackler cannot replace the pending foul.
    } else {
      REQUIRE(referee.foul.foul_tick == Tick{});
      REQUIRE(referee.foul.hasBeenProcessed);
    }
    REQUIRE(match.rng().engine() == rng);
  }
  RefereeStateFixture referee(&match);
  referee.buffer.active = false;
  const auto rng = match.rng().engine();
  referee.TripNotice(victim, tackler, 1, Tick{161}, Vector3(3, 4, 0));
  REQUIRE(referee.foul.foulType == 0); // Little standing trips are still ignored.
  referee.buffer.active = true;
  referee.TripNotice(nullptr, nullptr, 3, Tick{161}, Vector3(0)); // Gate precedes actor reads.
  REQUIRE(referee.foul.foulType == 0);
  REQUIRE(match.GetTimelineTick() == Tick{});
  REQUIRE(match.rng().engine() == rng);
  REQUIRE(match.GetBall()->Predict(0) == live_ball);
}

template<class T> concept HasImplicitTripNotice = requires(T& owner, Player* actor) {
  owner.TripNotice(actor, actor, 3);
};
static_assert(!HasImplicitTripNotice<Referee>);

template<class T> concept HasImplicitTouchSetter = requires(T& team, Player* player) {
  team.SetLastTouchPlayer(player, e_TouchType_Accidental);
};
static_assert(!HasImplicitTouchSetter<Team>);

TEST_CASE("the explicit touch sink is the only publication path for actor touches",
          "[sim][touchsink]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* team = match.GetTeam(0);
  auto* player = team->GetAllPlayers()[1];
  auto* other = match.GetTeam(1)->GetAllPlayers()[1];
  const auto rng = match.rng().engine();
  const auto now = Tick{77};
  auto& sink = SimulationAccess::EventsOf(simulation);
  sink.OnBallTouched({now, player, team, e_TouchType_Intentional_Nonkicked});
  REQUIRE(football::sim::event::LastTouchPlayer(match.touches(), *team) == player);
  REQUIRE(player->GetLastTouchTick() == now);
  REQUIRE(player->GetLastTouchType() == e_TouchType_Intentional_Nonkicked);
  REQUIRE(other->GetLastTouchTick() == Tick{});
  REQUIRE(match.GetLastTouchTeamID() == team->GetID());
  REQUIRE(match.GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == team->GetID());
  REQUIRE(match.rng().engine() == rng); // Publication is not a policy or RNG event.
}


TEST_CASE("touch notices consume explicit facts instead of Match state",
          "[sim][referee][offside]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* touch_player = match.GetTeam(0)->GetAllPlayers()[1];
  std::vector<Player*> active_players;
  match.GetTeam(0)->GetActivePlayers(active_players);
  match.GetTeam(1)->GetActivePlayers(active_players);

  SECTION("disabled offsides keeps prior facts and never dereferences Match") {
    RefereeStateFixture referee(&match);
    football::test::RuleCommandProbe commands;
    referee.buffer.active = false;
    referee.offsidePlayers = {touch_player};
    rules::BallTouchFacts facts;
    facts.now = Tick{5};
    facts.ball = match.GetBall();
    referee.BallTouched(facts, commands);
    REQUIRE(commands.calls.empty());
    REQUIRE(referee.offsidePlayers == std::vector<Player*>{touch_player});
  }

  SECTION("the supplied touch team decides the offside restart") {
    RefereeStateFixture referee(&match);
    referee.buffer.active = false;
    referee.offsidePlayers = {touch_player};
    const auto timeline = match.GetTimelineTick();
    REQUIRE(match.GetLastTouchTeamID() == -1); // Match state deliberately disagrees.
    rules::BallTouchFacts facts;
    facts.now = timeline;
    facts.touch_player = touch_player;
    facts.touch_team_id = 0;
    facts.touch_team = match.GetTeam(0);
    facts.defending_team = match.GetTeam(1);
    facts.in_play = true;
    facts.in_set_piece = false;
    facts.offsides_enabled = true;
    facts.ball = match.GetBall();
    facts.all_active_players = active_players;
    football::test::RuleCommandProbe commands(&SimulationAccess::CommandsOf(simulation));
    referee.BallTouched(facts, commands);
    REQUIRE(commands.calls == std::vector<std::string>{"stop"});
    REQUIRE(referee.buffer.active);
    REQUIRE(referee.buffer.desiredSetPiece == e_GameMode_FreeKick);
    REQUIRE(referee.buffer.teamID == 1);
    REQUIRE(referee.buffer.restartPos == touch_player->GetPitchPosition());
    REQUIRE_FALSE(match.IsInPlay());
    REQUIRE(match.GetLastTouchTeamID() == -1); // Still no Match touch publication.
  }
}

template<class T> concept HasImplicitFoulCheck = requires(T& referee) { referee.CheckFoul(); };
static_assert(!HasImplicitFoulCheck<Referee>);

TEST_CASE("foul evaluation timing comes from the supplied instant, not Match",
          "[sim][referee][foul]") {
  static constexpr TickSpan kRecheck{60};
  static constexpr TickSpan kExpiry{300};
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* offender = match.GetTeam(0)->GetAllPlayers()[1];
  auto* victim = match.GetTeam(1)->GetAllPlayers()[1];
  victim->GetTeam()->SetFadingTeamPossessionAmount(1.2f);
  RefereeStateFixture referee(&match);
  football::test::RuleCommandProbe commands;
  referee.buffer.active = false;
  referee.foul.foulPlayer = offender;
  referee.foul.foulVictim = victim;
  referee.foul.foulType = 1;
  referee.foul.advantage = true;
  referee.foul.foul_tick = Tick{100};
  referee.foul.hasBeenProcessed = false;
  REQUIRE_FALSE(referee.CheckFoul(Tick{100}, PitchFrameTransform(false), commands));
  REQUIRE_FALSE(referee.CheckFoul(Tick{100} + kRecheck, PitchFrameTransform(false), commands));
  REQUIRE_FALSE(referee.CheckFoul(Tick{100} + kRecheck + TickSpan{1}, PitchFrameTransform(false), commands));
  REQUIRE(referee.foul.advantage); // Kept while the victim side still has possession.
  REQUIRE(referee.foul.foulType == 1);
  REQUIRE(referee.foul.foulPlayer == offender); // Expires only past the advantage window.
  REQUIRE_FALSE(referee.CheckFoul(Tick{100} + kExpiry, PitchFrameTransform(false), commands));
  REQUIRE_FALSE(referee.CheckFoul(Tick{100} + kExpiry + TickSpan{1}, PitchFrameTransform(false), commands));
  REQUIRE(commands.calls.empty());
  REQUIRE(referee.foul.foulType == 0);
  REQUIRE(referee.foul.foulPlayer == nullptr);
}


static_assert(!std::is_constructible_v<Referee, Match*>);
static_assert(std::is_constructible_v<Referee, Team&, const Vector3&>);
template<class T> concept HasImplicitRuleTick = requires(T& referee) {
  referee.Process([](const Vector3&) {});
};
static_assert(!HasImplicitRuleTick<Referee>);

TEST_CASE("referee facts and write-only consequences do not fall back to runtime state",
          "[sim][referee][commands]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  RefereeStateFixture referee(&match);
  referee.buffer.active = false;
  football::test::RuleCommandProbe commands;
  auto facts = SimulationAccess::RefereeFactsOf(simulation);
  facts.phase = MatchPhase::FirstHalf;
  facts.now = Tick{9001};
  facts.play_authorized = true; // Match is still unauthorized PreMatch.
  facts.set_piece_active = false;
  match.GetBall()->ResetSituation(Vector3(56, 40, 0)); // Beyond BOTH lines.
  const auto rng = match.rng().engine();
  referee.Process(facts, match.options(), match.rng(), commands);
  REQUIRE(commands.calls == std::vector<std::string>{"stop"});
  REQUIRE(referee.buffer.desiredSetPiece == e_GameMode_GoalKick); // Stop prevents throw-in overwrite.
  REQUIRE(referee.buffer.stop_tick == facts.now);
  REQUIRE(referee.buffer.teamID == 1);
  REQUIRE(referee.buffer.restart);
  REQUIRE(referee.buffer.restart->earliest_restart_tick == facts.now + Seconds(3));
  REQUIRE(match.GetMatchPhase() == MatchPhase::PreMatch);
  REQUIRE(match.GetTimelineTick() == Tick{});
  REQUIRE(match.rng().engine() == rng);
}

TEST_CASE("foul commands schedule and card from supplied tick and stadium frame",
          "[sim][referee][commands][foul]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  RefereeStateFixture referee(&match);
  referee.buffer.active = false;
  referee.foul.foulPlayer = match.GetTeam(0)->GetAllPlayers()[1];
  referee.foul.foulVictim = match.GetTeam(1)->GetAllPlayers()[1];
  referee.foul.foulType = 2;
  referee.foul.foulPosition = Vector3(12, 6, 0);
  referee.foul.advantage = false;
  referee.foul.hasBeenProcessed = false;
  football::test::RuleCommandProbe commands;
  const auto rng = match.rng().engine();
  REQUIRE(referee.CheckFoul(Tick{8001}, PitchFrameTransform(true), commands));
  REQUIRE(commands.calls == std::vector<std::string>{"stop"});
  REQUIRE(referee.buffer.setpiece_team == match.GetTeam(1));
  REQUIRE(referee.buffer.restartPos == Vector3(-12, -6, 0));
  REQUIRE(referee.buffer.stop_tick == Tick{8001});
  REQUIRE(referee.buffer.restart->earliest_restart_tick == Tick{9001});
  REQUIRE(referee.buffer.restart->timeout_tick == Tick{12001});
  REQUIRE(referee.foul.foulPlayer->HasCards());
  REQUIRE_FALSE(referee.CheckFoul(Tick{9001}, PitchFrameTransform(false), commands));
  REQUIRE(commands.calls.size() == 1);
  REQUIRE(match.GetTimelineTick() == Tick{});
  REQUIRE(match.rng().engine() == rng);
}

TEST_CASE("Simulation uniquely owns independent rules across Init and Stop", "[sim][referee][owner]") {
  Simulation simulation;
  REQUIRE_THROWS_AS(SimulationAccess::RulesOf(simulation), std::logic_error);
  REQUIRE_THROWS_AS(SimulationAccess::CommandsOf(simulation), std::logic_error);
  for (bool reverse : {false, true}) {
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    auto& rules = SimulationAccess::RulesOf(simulation);
    REQUIRE(&rules == simulation.match()->GetReferee());
    REQUIRE(rules.GetBuffer().setpiece_team == simulation.match()->GetTeam(reverse ? 1 : 0));
    REQUIRE(rules.GetBuffer().teamID == (reverse ? 1 : 0));
    REQUIRE(rules.GetBuffer().endPhase);
    REQUIRE_FALSE(rules.GetBuffer().restart);
    simulation.Step({});
    REQUIRE(simulation.Stop());
    REQUIRE_THROWS_AS(SimulationAccess::RulesOf(simulation), std::logic_error);
    REQUIRE_THROWS_AS(SimulationAccess::CommandsOf(simulation), std::logic_error);
    REQUIRE_FALSE(simulation.Stop());
  }
}

template<class T> concept HasImplicitCompetitionTouchSetter = requires(T& owner) {
  owner.SetLastTouchTeamID(0);
};
static_assert(!HasImplicitCompetitionTouchSetter<Match>);

TEST_CASE("event dispatcher writes only supplied touch state and notifies rules synchronously",
          "[sim][touchsink][event]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* team = match.GetTeam(0);
  auto* player = team->GetAllPlayers()[1];
  auto& referee = SimulationAccess::RulesOf(simulation);
  football::sim::event::TouchState supplied;
  football::sim::rules::BallTouchFacts facts;
  facts.now = Tick{9};
  facts.ball = match.GetBall();
  football::test::RuleCommandProbe commands;
  const auto rng = match.rng().engine();
  football::sim::event::DispatchBallTouch(
      {Tick{77}, player, team, e_TouchType_Intentional_Nonkicked}, supplied, facts, referee, commands);
  REQUIRE(supplied.last_team == 0);
  REQUIRE(supplied.last_team_by_type[e_TouchType_Intentional_Nonkicked] == 0);
  REQUIRE(supplied.last_team_by_type[e_TouchType_Intentional_Kicked] == -1);
  REQUIRE(SimulationAccess::TouchesOf(simulation).last_team == -1);
  REQUIRE(player->GetLastTouchTick() == Tick{77});
  REQUIRE(football::sim::event::LastTouchPlayer(supplied, *team) == player);
  REQUIRE(football::sim::event::LastTouchPlayer(match.touches(), *team) == nullptr);
  REQUIRE(commands.calls.empty());
  REQUIRE(match.rng().engine() == rng);
  supplied.Reset();
  REQUIRE(supplied.last_team == -1);
  for (int team_id : supplied.last_team_by_type) REQUIRE(team_id == -1);
}

template<class T> concept HasTeamTouchRecord = requires(T& team) { team.GetLastTouchPlayer(); };
static_assert(!HasTeamTouchRecord<Team>);

TEST_CASE("event touch identities survive send-offs and reset without team bookkeeping",
          "[sim][touchsink][event][identity]") {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  home.players[1].id = 0xf0001234u;
  away.players[1].id = 0xe0005678u;
  Simulation simulation;
  simulation.Init(home, away, football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* home_actor = match.GetTeam(0)->GetAllPlayers()[1];
  auto* away_actor = match.GetTeam(1)->GetAllPlayers()[1];
  auto& events = SimulationAccess::EventsOf(simulation);
  const auto rng = match.rng().engine();
  events.OnBallTouched({Tick{17}, home_actor, match.GetTeam(0), e_TouchType_Intentional_Kicked});
  events.OnBallTouched({Tick{19}, away_actor, match.GetTeam(1), e_TouchType_Accidental});
  const auto& touches = match.touches();
  REQUIRE(touches.last_player_by_team[0] == home.players[1].id);
  REQUIRE(touches.last_player_by_team[1] == away.players[1].id);
  REQUIRE(touches.last_team == 1);
  REQUIRE(touches.last_team_by_type[e_TouchType_Intentional_Kicked] == 0);
  REQUIRE(touches.last_team_by_type[e_TouchType_Accidental] == 1);
  REQUIRE(football::sim::event::TeamTouchBias(touches, *match.GetTeam(0), 503, Tick{23}) ==
          home_actor->GetLastTouchBias(503, Tick{23}));
  REQUIRE(match.rng().engine() == rng);
  away_actor->SendOff();
  REQUIRE_FALSE(away_actor->IsActive());
  REQUIRE(football::sim::event::LastTouchPlayer(touches, *match.GetTeam(1)) == away_actor);
  simulation.ResetSituation(Vector3(0));
  REQUIRE(touches.last_team == -1);
  for (auto id : touches.last_player_by_team) REQUIRE(id == football::model::kInvalidPlayerId);
  REQUIRE(football::sim::event::LastTouchPlayer(touches, *match.GetTeam(0)) == nullptr);
  REQUIRE(football::sim::event::TeamTouchBias(touches, *match.GetTeam(1), 503, Tick{23}) == 0.f);
}
}  // namespace

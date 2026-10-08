#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>

#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "default_ai_fixture.hpp"
#include "sim/rules/period.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace {
using namespace football::sim;
using football::sim::testing::SimulationAccess;
template<class T> concept HasImplicitPeriodQuery = requires(const T& owner) { owner.PeriodElapsed(); };
static_assert(!HasImplicitPeriodQuery<Referee>);
void Init(Simulation& simulation, MatchOptions options = {}) {
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options);
}
void StartHalf(Simulation& simulation) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int attempts = 0; !simulation.Observe().ball_in_play; ++attempts) {
    REQUIRE(attempts < 1000);
    REQUIRE_FALSE(simulation.Finished());
    football::test::StepDefaultAI(simulation, policy);
  }
}
std::uint64_t Finish(Simulation& simulation) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  std::uint64_t steps = 0;
  while (!simulation.Finished()) {
    football::test::StepDefaultAI(simulation, policy);
    REQUIRE(++steps < 10000);
  }
  return steps;
}
}

TEST_CASE("three clocks distinguish half running, authorization and actual ball play", "[sim][lifecycle][clock]") {
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.half_duration = TickSpan{180};
    options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    const auto initial = simulation.Observe();
    REQUIRE(initial.phase == MatchPhase::PreMatch);
    REQUIRE(initial.regulation_time == TickSpan{});
    REQUIRE(initial.ball_in_play_time == TickSpan{});
    REQUIRE_FALSE(initial.half_underway);
    REQUIRE_FALSE(initial.ball_in_play);
    REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    const auto policy = football::test::MakeDefaultAI(simulation);
    std::uint64_t steps = 0;
    bool second = false, second_play = false, break_seen = false;
    while (!simulation.Finished()) {
      const auto before = simulation.Observe();
      football::test::StepDefaultAI(simulation, policy);
      REQUIRE(++steps < 3000);
      const auto after = simulation.Observe();
      REQUIRE(after.regulation_time == SimulationAccess::RegulationTimeOf(simulation));
      REQUIRE(after.ball_in_play_time == SimulationAccess::BallInPlayTimeOf(simulation));
      REQUIRE(after.phase == SimulationAccess::PhaseOf(simulation));
      REQUIRE(after.regulation_time >= before.regulation_time);
      REQUIRE(after.ball_in_play_time >= before.ball_in_play_time);
      REQUIRE(after.regulation_time - before.regulation_time <= TickSpan{1});
      REQUIRE(after.ball_in_play_time - before.ball_in_play_time <=
              after.regulation_time - before.regulation_time);
      REQUIRE(after.ball_in_play_time <= after.regulation_time);
      REQUIRE(after.regulation_time.value <= after.tick);
      if (!before.half_underway && !after.half_underway)
        REQUIRE(before.regulation_time == after.regulation_time);
      if (after.phase == MatchPhase::SecondHalf) {
        second = true; second_play |= after.ball_in_play;
        break_seen |= !after.half_underway;
        REQUIRE(after.regulation_time >= options.half_duration);
      }
    }
    REQUIRE(second); REQUIRE(second_play); REQUIRE(break_seen);
    const auto world = simulation.Observe();
    const auto result = simulation.Result();
    REQUIRE(world.phase == MatchPhase::Finished);
    REQUIRE(world.regulation_time == options.half_duration + options.half_duration);
    REQUIRE_FALSE(world.half_underway); REQUIRE_FALSE(world.ball_in_play);
    REQUIRE_FALSE(world.in_play); REQUIRE_FALSE(world.in_set_piece);
    REQUIRE(world.restart == e_GameMode_Normal); REQUIRE_FALSE(world.restart_taker);
    REQUIRE(result.duration_ticks == steps);
    REQUIRE(world.tick + 1 == steps); // Terminal whistle does not execute physics/time.
    REQUIRE(result.outcome == MatchOutcome::Draw);
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    for (int tick = 0; tick < 20; ++tick) simulation.Step({});
    simulation.AdvanceTime(Seconds(1)); // Direct authority is frozen too.
    REQUIRE(simulation.Result() == result);
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    REQUIRE(simulation.Observe().tick == world.tick);
    REQUIRE(simulation.Observe().ball_position == world.ball_position);
    REQUIRE(simulation.Observe().regulation_time == world.regulation_time);
    REQUIRE(simulation.Observe().ball_in_play_time == world.ball_in_play_time);
    simulation.Stop(); REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    REQUIRE(initial.phase == MatchPhase::PreMatch);
    REQUIRE(initial.regulation_time == TickSpan{});
    Init(simulation, options);
    REQUIRE(Finish(simulation) == steps);
    REQUIRE(simulation.Result() == result);
  }
}

TEST_CASE("idle kickoff authorization cannot start either football clock", "[sim][lifecycle][clock]") {
  for (bool reverse : {false, true}) {
    MatchOptions options; options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    for (int tick = 0; tick < 500; ++tick) simulation.Step({});
    const auto ready = simulation.Observe();
    REQUIRE(ready.tick == 500);
    REQUIRE(ready.in_play); REQUIRE(ready.in_set_piece);
    REQUIRE_FALSE(ready.half_underway); REQUIRE_FALSE(ready.ball_in_play);
    REQUIRE(ready.regulation_time == TickSpan{});
    REQUIRE(ready.ball_in_play_time == TickSpan{});
    REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::Ready);
    const auto* taker = SimulationAccess::RefereeOf(simulation)->GetBuffer().taker;
    for (int side : {0, 1})
      for (const auto* actor : SimulationAccess::TeamOf(simulation, side)->GetAllPlayers())
        REQUIRE(SimulationAccess::MayTouchBallOf(simulation, *actor) == (actor == taker));
    StartHalf(simulation);
    const auto taken = simulation.Observe();
    REQUIRE(taken.half_underway); REQUIRE(taken.ball_in_play);
    REQUIRE(taken.regulation_time == TickSpan{1});
    REQUIRE(taken.ball_in_play_time == TickSpan{1});
    const auto& buffer = SimulationAccess::RefereeOf(simulation)->GetBuffer();
    REQUIRE(buffer.restart->phase == RestartPhase::Taken);
    REQUIRE(buffer.taker->GetLastTouchTick().value + 1 == taken.tick);
    REQUIRE(SimulationAccess::BallOf(simulation)->GetMovement().GetLength() > 0.1f);
    for (int side : {0, 1})
      for (const auto* actor : SimulationAccess::TeamOf(simulation, side)->GetAllPlayers())
        REQUIRE(SimulationAccess::MayTouchBallOf(simulation, *actor));
  }
}

TEST_CASE("one-tick regulation periods each require their own actual kickoff", "[sim][lifecycle][clock]") {
  MatchOptions options; options.half_duration = TickSpan{1};
  Simulation simulation; Init(simulation, options);
  bool first = false, second = false;
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int tick = 0; tick < 3000 && !simulation.Finished(); ++tick) {
    football::test::StepDefaultAI(simulation, policy);
    const auto world = simulation.Observe();
    first |= world.phase == MatchPhase::FirstHalf && world.ball_in_play;
    second |= world.phase == MatchPhase::SecondHalf && world.ball_in_play;
  }
  REQUIRE(first); REQUIRE(second); REQUIRE(simulation.Finished());
  REQUIRE(simulation.Observe().regulation_time == TickSpan{2});
  REQUIRE(simulation.Observe().ball_in_play_time == TickSpan{2});
}

TEST_CASE("period whistles win over pending dead balls and half-time stops regulation", "[sim][lifecycle][clock][restart]") {
  for (bool reverse : {false, true}) {
    MatchOptions options; options.half_duration = Seconds(10);
    options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    for (int half = 1; half <= 2; ++half) {
      StartHalf(simulation);
      // Release Taken and expire relaxation before forcing a native ordinary out.
      for (int tick = 0; tick < 41; ++tick) simulation.Step({});
      SimulationAccess::BallOf(simulation)->ResetSituation(blunted::Vector3(10.f, 40.f, 0.f));
      simulation.Step({}); simulation.Step({});
      REQUIRE(simulation.Observe().restart_pending);
      REQUIRE(simulation.Observe().half_underway);
      REQUIRE_FALSE(simulation.Observe().ball_in_play);
      const auto resets = SimulationAccess::ResetSequenceOf(simulation);
      const auto rng = SimulationAccess::RngOf(simulation).engine();
      const auto in_play = SimulationAccess::BallInPlayTimeOf(simulation);
      const auto position = simulation.Observe().ball_position;
      simulation.AdvanceTime(Seconds(100));
      REQUIRE(SimulationAccess::RegulationTimeOf(simulation) == (half == 1 ? Seconds(10) : Seconds(20)));
      REQUIRE(rules::PeriodElapsed(SimulationAccess::IsHalfUnderwayOf(simulation), SimulationAccess::PhaseOf(simulation),
                                   SimulationAccess::RegulationTimeOf(simulation), options.half_duration));
      REQUIRE(SimulationAccess::BallInPlayTimeOf(simulation) == in_play);
      REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
      const auto whistle_tick = SimulationAccess::NowOf(simulation);
      const int home_side = SimulationAccess::TeamOf(simulation, 0)->GetStaticSide();
      simulation.Step({});
      REQUIRE_FALSE(simulation.Observe().half_underway);
      REQUIRE_FALSE(rules::PeriodElapsed(SimulationAccess::IsHalfUnderwayOf(simulation), SimulationAccess::PhaseOf(simulation),
                                         SimulationAccess::RegulationTimeOf(simulation), options.half_duration));
      REQUIRE_FALSE(simulation.Observe().ball_in_play);
      REQUIRE_FALSE(SimulationAccess::RefereeOf(simulation)->RestartNeedsSimulation());
      REQUIRE(SimulationAccess::ResetSequenceOf(simulation) == resets);
      REQUIRE(simulation.Observe().ball_position == position);
      REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
      REQUIRE(SimulationAccess::TeamOf(simulation, 0)->GetStaticSide() == home_side);
      REQUIRE_FALSE(SimulationAccess::IsBallMirroredOf(simulation));
      REQUIRE_FALSE(SimulationAccess::TeamOf(simulation, 0)->isMirrored());
      REQUIRE_FALSE(SimulationAccess::TeamOf(simulation, 1)->isMirrored());
      REQUIRE(SimulationAccess::NowOf(simulation) == whistle_tick + TickSpan{half == 1 ? 1u : 0u});
      if (half == 1) {
        REQUIRE(simulation.Observe().phase == MatchPhase::SecondHalf);
        const auto& kickoff = SimulationAccess::RefereeOf(simulation)->GetBuffer();
        REQUIRE(kickoff.stop_tick == whistle_tick);
        REQUIRE(kickoff.prepare_tick == whistle_tick + TickSpan{10});
        REQUIRE(kickoff.start_tick == whistle_tick + TickSpan{30});
        REQUIRE(kickoff.teamID == (SimulationAccess::OptionsOf(simulation).left_team_owns_ball ? 1 : 0));
        REQUIRE(kickoff.taker == nullptr);
        REQUIRE(kickoff.active);
        REQUIRE(kickoff.endPhase);
        const auto before = simulation.Observe();
        for (int tick = 0; tick < 60; ++tick) {
          simulation.Step({});
          REQUIRE(simulation.Observe().regulation_time == before.regulation_time);
          REQUIRE(simulation.Observe().ball_in_play_time == before.ball_in_play_time);
        }
        REQUIRE(simulation.Observe().in_set_piece); // Authorized, not actually taken.
        REQUIRE(SimulationAccess::TeamOf(simulation, 0)->GetStaticSide() == -home_side);
      }
    }
    REQUIRE(simulation.Finished());
    REQUIRE_FALSE(SimulationAccess::RefereeOf(simulation)->GetBuffer().active);
    REQUIRE(simulation.Result().outcome == MatchOutcome::Draw);
  }
}

TEST_CASE("final outcomes derive from real goals and stable home away scores", "[sim][lifecycle]") {
  for (bool home_win : {true, false}) {
    MatchOptions options; options.half_duration = TickSpan{180};
    Simulation simulation; Init(simulation, options); StartHalf(simulation);
    const float direction = home_win ? 1.f : -1.f;
    SimulationAccess::BallOf(simulation)->ResetSituation(blunted::Vector3(direction * 54.9f, 0.f, 0.5f));
    simulation.TouchBall(blunted::Vector3(direction * 30.f, 0.f, 0.f));
    for (int step = 0; step < 5 && simulation.Observe().teams[home_win ? 0 : 1].score == 0; ++step)
      simulation.Step({});
    REQUIRE(simulation.Observe().teams[home_win ? 0 : 1].score == 1);
    REQUIRE_FALSE(simulation.Observe().ball_in_play);
    Finish(simulation);
    const auto result = simulation.Result();
    REQUIRE(result.home_score == (home_win ? 1 : 0));
    REQUIRE(result.away_score == (home_win ? 0 : 1));
    REQUIRE(result.outcome == (home_win ? MatchOutcome::HomeWin : MatchOutcome::AwayWin));
  }
}

TEST_CASE("invalid native durations fail before match publication or RNG consumption", "[sim][lifecycle][clock]") {
  Simulation simulation;
  for (auto duration : {TickSpan{}, TickSpan{std::numeric_limits<std::uint64_t>::max()},
                         TickSpan{std::numeric_limits<std::uint64_t>::max() / 2 + 1}}) {
    MatchOptions options; options.half_duration = duration;
    REQUIRE_THROWS_AS(Init(simulation, options), std::invalid_argument);
    REQUIRE_FALSE(SimulationAccess::IsInitializedOf(simulation)); REQUIRE_FALSE(simulation.Finished());
    REQUIRE_THROWS_AS(simulation.Result(), std::logic_error);
    REQUIRE_THROWS_AS(simulation.Observe(), std::logic_error);
    REQUIRE_THROWS_AS(simulation.Step({}), std::logic_error);
  }
  Init(simulation);
  Simulation fresh; Init(fresh);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == SimulationAccess::RngOf(fresh).engine());
  simulation.Stop();
  MatchOptions large; large.half_duration = TickSpan{std::numeric_limits<std::uint64_t>::max() / 2};
  REQUIRE_NOTHROW(Init(simulation, large)); // No spurious millisecond-capacity restriction.
}

TEST_CASE("native clocks retain integer precision and advance atomically", "[sim][lifecycle][clock]") {
  MatchOptions options; options.half_duration = TickSpan{UINT64_C(1) << 40};
  Simulation simulation; Init(simulation, options); StartHalf(simulation);
  simulation.AdvanceTime(TickSpan{UINT64_C(1) << 25});
  const auto before = simulation.Observe();
  const auto rng = SimulationAccess::RngOf(simulation).engine();
  simulation.AdvanceTime(TickSpan{1});
  REQUIRE(simulation.Observe().regulation_time == before.regulation_time + TickSpan{1});
  REQUIRE(simulation.Observe().ball_in_play_time == before.ball_in_play_time + TickSpan{1});
  REQUIRE(simulation.Observe().tick == before.tick + 1);
  REQUIRE(simulation.Observe().ball_position == before.ball_position);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
  const auto stable = simulation.Observe();
  REQUIRE_THROWS_AS(simulation.AdvanceTime(TickSpan{std::numeric_limits<std::uint64_t>::max()}),
                    std::overflow_error);
  REQUIRE(simulation.Observe().tick == stable.tick);
  REQUIRE(simulation.Observe().regulation_time == stable.regulation_time);
  REQUIRE(simulation.Observe().ball_in_play_time == stable.ball_in_play_time);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
}

TEST_CASE("half time changes ends but keeps observations in the canonical home frame", "[sim][lifecycle]") {
  const auto observed_x = [](const Simulation& simulation, int team) {
    return simulation.Observe().players[team == 0 ? 0 : 11].position.coords[0];
  };
  const auto keeper_pitch_x = [](Simulation& simulation, int team) {
    return SimulationAccess::TeamOf(simulation, team)->GetAllPlayers()[0]->GetPitchPosition().coords[0];
  };
  MatchOptions options; options.half_duration = TickSpan{180};
  Simulation simulation; Init(simulation, options); StartHalf(simulation);
  const auto policy = football::test::MakeDefaultAI(simulation);
  REQUIRE(simulation.Observe().phase == MatchPhase::FirstHalf);
  REQUIRE(SimulationAccess::TeamOf(simulation, 0)->GetStaticSide() == -1);
  REQUIRE(SimulationAccess::TeamOf(simulation, 1)->GetStaticSide() == 1);
  REQUIRE(keeper_pitch_x(simulation, 0) < 0.f); REQUIRE(keeper_pitch_x(simulation, 1) > 0.f);
  REQUIRE(observed_x(simulation, 0) < 0.f); REQUIRE(observed_x(simulation, 1) > 0.f);
  for (int attempts = 0; !(simulation.Observe().phase == MatchPhase::SecondHalf &&
                            simulation.Observe().ball_in_play); ++attempts) {
    REQUIRE(attempts < 1000); REQUIRE_FALSE(simulation.Finished());
    football::test::StepDefaultAI(simulation, policy);
  }
  REQUIRE(SimulationAccess::TeamOf(simulation, 0)->GetStaticSide() == 1);
  REQUIRE(SimulationAccess::TeamOf(simulation, 1)->GetStaticSide() == -1);
  REQUIRE(keeper_pitch_x(simulation, 0) > 0.f); REQUIRE(keeper_pitch_x(simulation, 1) < 0.f);
  REQUIRE(observed_x(simulation, 0) < 0.f); REQUIRE(observed_x(simulation, 1) > 0.f);
  REQUIRE(simulation.Observe().teams[0].defending_direction == -1);
  REQUIRE(simulation.Observe().teams[1].defending_direction == 1);
  REQUIRE_FALSE(SimulationAccess::IsBallMirroredOf(simulation));
  REQUIRE_FALSE(SimulationAccess::TeamOf(simulation, 0)->isMirrored());
  REQUIRE_FALSE(SimulationAccess::TeamOf(simulation, 1)->isMirrored());
}

TEST_CASE("the same physical goal credits opposite teams after advancing clocks", "[sim][lifecycle][goal]") {
  const auto credited = [](MatchPhase wanted, float direction, bool reverse) {
    MatchOptions options; options.half_duration = TickSpan{180};
    options.reverse_team_processing = reverse;
    Simulation simulation; Init(simulation, options);
    const auto policy = football::test::MakeDefaultAI(simulation);
    for (int attempt = 0; attempt < 3000 && !simulation.Finished(); ++attempt) {
      if (simulation.Observe().phase == wanted && simulation.Observe().ball_in_play) break;
      football::test::StepDefaultAI(simulation, policy);
    }
    REQUIRE(simulation.Observe().phase == wanted);
    REQUIRE(simulation.Observe().ball_in_play);
    const int before[2] = {SimulationAccess::ScoreOf(simulation, 0), SimulationAccess::ScoreOf(simulation, 1)};
    const float execution_direction = reverse ? -direction : direction;
    SimulationAccess::BallOf(simulation)->ResetSituation(blunted::Vector3(execution_direction * 54.9f, 0.f, 0.5f));
    simulation.TouchBall(blunted::Vector3(execution_direction * 30.f, 0.f, 0.f));
    for (int step = 0; step < 12; ++step) {
      const auto timeline = SimulationAccess::NowOf(simulation);
      const auto regulation = SimulationAccess::RegulationTimeOf(simulation);
      const auto effective = SimulationAccess::BallInPlayTimeOf(simulation);
      simulation.Step({});
      for (int team : {0, 1}) {
        if (SimulationAccess::ScoreOf(simulation, team) == before[team]) continue;
        REQUIRE(SimulationAccess::NowOf(simulation) == timeline + TickSpan{1});
        REQUIRE(SimulationAccess::RegulationTimeOf(simulation) == regulation + TickSpan{1});
        REQUIRE(SimulationAccess::BallInPlayTimeOf(simulation) == effective + TickSpan{1});
        REQUIRE(SimulationAccess::LastGoalTeamOf(simulation) == SimulationAccess::TeamOf(simulation, team));
        REQUIRE(SimulationAccess::IsBallInGoalOf(simulation));
        REQUIRE(SimulationAccess::IsGoalScoredOf(simulation));
        REQUIRE_FALSE(SimulationAccess::IsBallInPlayOf(simulation));
        return team;
      }
    }
    return -1;
  };
  for (bool reverse : {false, true}) {
    REQUIRE(credited(MatchPhase::FirstHalf, 1.f, reverse) == 0);
    REQUIRE(credited(MatchPhase::FirstHalf, -1.f, reverse) == 1);
    REQUIRE(credited(MatchPhase::SecondHalf, 1.f, reverse) == 1);
    REQUIRE(credited(MatchPhase::SecondHalf, -1.f, reverse) == 0);
  }
}
